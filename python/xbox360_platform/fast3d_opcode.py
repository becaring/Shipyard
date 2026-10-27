"""Lower pinned Fast3D opcode tables for the XDK compiler."""

import re

FAST3D_TABLES = ("rdpHandlers", "otrHandlers", "f3dex2Handlers", "f3dexHandlers", "f3dHandlers", "s2dexHandlers")


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Expected exactly one opcode anchor: " + old[:80])
    return text.replace(old, new, 1)


def remove_unused_directory(text):
    """Drop write-only directory storage; reject any new reader in pinned input."""
    if len(re.findall(r"\bcurrentDir\b", text)) != 3 or len(re.findall(r"\bGetPathWithoutFileName\b", text)) != 2:
        raise ValueError("Directory storage reference inventory changed")
    text = _once(text, "std::stack<std::string> currentDir;", "")
    text = _once(text, "    currentDir = std::stack<std::string>();", "")
    helper = re.compile(r"^static std::string GetPathWithoutFileName\(char\* filePath\) \{.*?^\}", re.M | re.S)
    text, count = helper.subn("", text)
    if count != 1:
        raise ValueError("Directory parser shape changed")
    push = re.compile(r"^void gfx_push_current_dir\(char\* path\) \{.*?^\}", re.M | re.S)
    original = push.findall(text)
    expected = """\
void gfx_push_current_dir(char* path) { if (gfx_check_image_signature(path) == 1) path = &path[7]; \
currentDir.push(GetPathWithoutFileName(path)); }"""
    if len(original) != 1 or " ".join(original[0].split()) != expected:
        raise ValueError("Directory command shape changed")
    text = push.sub("void gfx_push_current_dir(char* path) {\n    (void)gfx_check_image_signature(path);\n}", text)
    return text


def lower(text, parser, walk, expected_tables, *, composed_dispatch=False):
    if "gfx_push_current_dir" in text:
        text = remove_unused_directory(text)
    start = text.index("class UcodeHandler {")
    end = text.index("\n};", start) + 3
    cls = text[start:end]
    old = """    inline constexpr UcodeHandler(
        std::initializer_list<std::pair<int8_t, std::pair<const char*, GfxOpcodeHandlerFunc>>> initializer) {
        std::fill(std::begin(mHandlers), std::end(mHandlers),
                  std::pair<const char*, GfxOpcodeHandlerFunc>(nullptr, nullptr));

        for (const auto& [opcode, handler] : initializer) {
            mHandlers[static_cast<uint8_t>(opcode)] = handler;
        }
    }"""
    new = """    UcodeHandler(const UcodeEntry* initializer, size_t count) {
        std::fill(mHandlers, mHandlers + sizeof(mHandlers)/sizeof(mHandlers[0]),
                  std::pair<const char*, GfxOpcodeHandlerFunc>(nullptr, nullptr));
        for (size_t i=0; i<count; ++i) {
            const UcodeEntry& entry=initializer[i];
            mHandlers[static_cast<uint8_t>(entry.opcode)] = std::make_pair(entry.name, entry.handler);
        }
    }"""
    cls = _once(cls, old, new)
    cls = _once(cls, "std::numeric_limits<uint8_t>::max() + 1", "256 /* complete uint8_t opcode domain */")
    if composed_dispatch:
        cls = _once(
            cls,
            "    inline std::pair<const char*, GfxOpcodeHandlerFunc> at(int8_t opcode) const {\n"
            "        return mHandlers[static_cast<uint8_t>(opcode)];\n"
            "    }",
            "    inline const std::pair<const char*, GfxOpcodeHandlerFunc>& at(int8_t opcode) const {\n"
            "        return mHandlers[static_cast<uint8_t>(opcode)];\n"
            "    }",
        )
        cls = _once(
            cls,
            "  private:\n",
            """    UcodeHandler(const UcodeHandler& otr, const UcodeHandler& rdp,
                     const UcodeHandler& ucode) {
            for (unsigned i = 0; i < 256; ++i) {
                const std::pair<const char*, GfxOpcodeHandlerFunc>& o = otr.mHandlers[i];
                const std::pair<const char*, GfxOpcodeHandlerFunc>& r = rdp.mHandlers[i];
                mHandlers[i] = o.first ? o : (r.first ? r : ucode.mHandlers[i]);
            }
        }

      private:
    """,
        )
    text = (
        text[:start]
        + "struct UcodeEntry { int8_t opcode; const char* name; GfxOpcodeHandlerFunc handler; };\n"
        + cls
        + text[end:]
    )
    source = text.encode()
    edits = []
    tables = []
    for node in walk(parser.parse(source).root_node):
        if node.type != "declaration" or not node.text.startswith(b"static constexpr UcodeHandler "):
            continue
        declarator = node.child_by_field_name("declarator")
        name = declarator.child_by_field_name("declarator").text.decode()
        values = declarator.child_by_field_name("value")
        rows = []
        for entry in values.named_children:
            if entry.type == "comment":
                continue
            fields = [field for field in entry.named_children if field.type != "comment"]
            if len(fields) != 2 or fields[1].type != "initializer_list":
                raise ValueError("Opcode entry shape drift")
            pair = [field for field in fields[1].named_children if field.type != "comment"]
            if len(pair) != 2:
                raise ValueError("Opcode pair shape drift")
            rows.append(
                "{ " + fields[0].text.decode() + ", " + pair[0].text.decode() + ", " + pair[1].text.decode() + " }"
            )
        if not rows:
            raise ValueError("Empty opcode table: " + name)
        tables.append(dict(name=name, count=len(rows)))
        replacement = "static const UcodeEntry " + name + "Entries[] = {\n    " + ",\n    ".join(rows) + "\n};\n"
        replacement += (
            "static const UcodeHandler "
            + name
            + "("
            + name
            + "Entries, sizeof("
            + name
            + "Entries)/sizeof("
            + name
            + "Entries[0]));"
        )
        edits.append((node.start_byte, node.end_byte, replacement.encode()))
    if tuple(table["name"] for table in tables) != tuple(expected_tables):
        raise ValueError("Opcode table inventory drift: " + repr(tables))
    for start, end, replacement in sorted(edits, reverse=True):
        source = source[:start] + replacement + source[end:]
    if not composed_dispatch:
        return source.decode(), tables
    return _compose(source.decode()), tables


def _block(lines, what):
    """CALL from an exact `if (CALL) {` / `return;` / `}` dispatch block."""
    match = re.fullmatch(r"( +)if \((.*)\) \{", lines[0]) if len(lines) == 3 else None
    if not match or lines[1] != match[1] + "    return;" or lines[2] != match[1] + "}":
        raise ValueError("Opcode dispatch block changed: " + what)
    return match[2]


def _branches(region):
    """Split the OTR/RDP/ucode/invalid chain into (condition, body lines)."""
    branches = []
    for line in region.rstrip("\n").split("\n"):
        head = re.fullmatch(r"    (?:if|\} else if) \((.*)\) \{|    \} else \{|    \}", line)
        if head:
            branches.append((head[1] if head[1] is not None else line.strip(), []))
        elif not branches:
            raise ValueError("Opcode dispatch chain changed")
        else:
            branches[-1][1].append(line)
    if [b[0] for b in branches][:2] != ["otrHandlers.contains(opcode)", "rdpHandlers.contains(opcode)"] or \
            len(branches) != 5 or branches[3][0] != "} else {" or branches[4] != ("}", []):
        raise ValueError("Opcode dispatch chain changed")
    return branches[:4]


def _guard(lines):
    """Hoist a consumer's OTR argument guard ahead of the composed lookup.

    The guard's own opcode tests run first, so other commands pay nothing;
    it still applies only to commands the OTR table owns, as before.
    """
    body = [line[4:] if line.startswith("        ") else line for line in lines]
    first = next((i for i, line in enumerate(body) if line.startswith("    if (")), None)
    if first is None:
        if any(line.strip() and not line.strip().startswith("//") for line in body):
            raise ValueError("Opcode dispatch guard changed")
        return ""
    last = next(i for i in range(first, len(body)) if body[i].endswith(") {"))
    if body[-1] != "    }" or any(line.strip() and not line.strip().startswith("//") for line in body[:first]):
        raise ValueError("Opcode dispatch guard changed")
    body[first] = body[first].replace("    if (", "    if ((", 1)
    body[last] = body[last][: -len(") {")] + ") && otrHandlers.contains(opcode)) {"
    return "\n".join(body) + "\n"


def _compose(text):
    """Index one composed OTR > RDP > ucode table per command.

    Every consumer statement in the original chain is kept: OTR argument
    guards, dispatch wrappers (pipeline stamps) and unhandled-opcode bodies.
    An invalid ucode keeps the independent OTR/RDP lookups.
    """
    decl = re.search(r"static (constexpr std::array ucode_handlers|const UcodeHandler\* const ucode_handlers\[\]) = \{",
                     text)
    if not decl or len(re.findall(r"\bucode_handlers(?:\[\])? = \{", text)) != 1:
        raise ValueError("Opcode dispatch ucode table changed")
    start = decl.start()
    end = text.index("\n};", start) + len("\n};")
    handlers = text[start:end]
    for base, expected in [("f3d", 2), ("f3dex", 2), ("f3dex2", 1), ("s2dex", 1)]:
        old = "&" + base + "Handlers"
        if handlers.count(old) != expected:
            raise ValueError("Opcode dispatch ucode inventory drift: " + base)
        handlers = handlers.replace(old, "&" + base + "Dispatch")
    if decl[1].startswith("constexpr"):
        handlers = handlers.replace(decl[0], "static const std::array<const UcodeHandler*, 6> ucode_handlers = { {", 1)
        handlers = handlers[:-3] + "\n} };"
    dispatch = """static const UcodeHandler f3dDispatch(otrHandlers, rdpHandlers, f3dHandlers);
static const UcodeHandler f3dexDispatch(otrHandlers, rdpHandlers, f3dexHandlers);
static const UcodeHandler f3dex2Dispatch(otrHandlers, rdpHandlers, f3dex2Handlers);
static const UcodeHandler s2dexDispatch(otrHandlers, rdpHandlers, s2dexHandlers);

"""
    text = text[:start] + dispatch + handlers + text[end:]
    name_start = text.index("const char* GfxGetOpcodeName(int8_t opcode) {")
    name_end = text.index("\n}", name_start)
    old_name = text[name_start:name_end]
    if old_name.count("otrHandlers.contains(opcode)") != 1 or old_name.count("rdpHandlers.contains(opcode)") != 1:
        raise ValueError("Opcode name dispatch anchor changed")
    size = re.findall(r"if \((ucode_handler_index < .+)\) \{", old_name)
    if len(size) != 1:
        raise ValueError("Opcode name dispatch anchor changed")
    new_name = """const char* GfxGetOpcodeName(int8_t opcode) {
    if (%s) {
        const auto& handler = ucode_handlers[ucode_handler_index]->at(opcode);
        if (handler.first) return handler.first;
        SPDLOG_CRITICAL("Unhandled OP code: 0x{:X}, for loaded ucode: {}", (uint8_t)opcode,
                        (uint32_t)ucode_handler_index);
    } else {
        // Preserve the original OTR/RDP priority even for an invalid ucode.
        if (otrHandlers.contains(opcode)) return otrHandlers.at(opcode).first;
        if (rdpHandlers.contains(opcode)) return rdpHandlers.at(opcode).first;
        SPDLOG_CRITICAL("Unhandled OP code: 0x{:X}, invalid ucode: {}", (uint8_t)opcode,
                        (uint32_t)ucode_handler_index);
    }
    return nullptr;""" % size[0]
    text = text[:name_start] + new_name + text[name_end:]
    steps = list(re.finditer(r"static void gfx_step\([^)]*\) \{", text))
    if len(steps) != 1:
        raise ValueError("Opcode step anchor changed")
    step_start = steps[0].start()
    step_end = text.index("\n}", step_start)
    step = text[step_start:step_end]
    dispatch_start = step.index("    if (otrHandlers.contains(opcode)) {")
    dispatch_end = step.index("\n    ++cmd;", dispatch_start)
    (_, otr), (_, rdp), (ucode_size, ucode), (_, invalid) = _branches(step[dispatch_start:dispatch_end])
    split = max(i for i, line in enumerate(otr) if line.startswith("        if (") and "otrHandlers.at(opcode).second" in line)
    otr_call = _block(otr[split:], "OTR")
    rdp_call = _block(rdp, "RDP")
    owned = "        if (ucode_handlers[ucode_handler_index]->contains(opcode)) {"
    if not ucode_size.startswith("ucode_handler_index < ") or ucode[0] != owned or ucode[4] != "        } else {" or \
            ucode[-1] != "        }":
        raise ValueError("Opcode dispatch ucode branch changed")
    ucode_call = _block(ucode[1:4], "ucode")
    if ucode_call.count("ucode_handlers[ucode_handler_index]->at(opcode).second") != 1:
        raise ValueError("Opcode dispatch ucode call changed")
    ucode_call = ucode_call.replace("ucode_handlers[ucode_handler_index]->at(opcode).second", "handler.second")
    new_step = _guard(otr[:split]) + """    if (%s) {
        const auto& handler = ucode_handlers[ucode_handler_index]->at(opcode);
        if (handler.first) {
            if (%s) return;
        } else {
%s
        }
    } else {
        // Invalid ucode retains the independent OTR/RDP dispatch paths.
        if (otrHandlers.contains(opcode)) {
            if (%s) return;
        } else if (rdpHandlers.contains(opcode)) {
            if (%s) return;
        } else {
%s
        }
    }
""" % (ucode_size, ucode_call, "\n".join(ucode[5:-1]), otr_call, rdp_call,
       "\n".join(("    " + line) if line.strip() and not line.startswith("#") else line for line in invalid))
    step = step[:dispatch_start] + new_step + step[dispatch_end:]
    return text[:step_start] + step + text[step_end:]
