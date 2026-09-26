"""Strict C++ syntax lowering for pinned ImGui sources on the XDK compiler."""

import re

from .cpp_syntax import collect_member_defaults


def _nodes(node):
    yield node
    for child in node.named_children:
        yield from _nodes(child)


def _replace_bytes(data, edits):
    for start, end, replacement in sorted(edits, reverse=True):
        data = data[:start] + replacement + data[end:]
    return data


def move_enum_definitions(text, expected):
    """Move unique full definitions to their opaque-declaration sites."""
    declaration = re.compile(r"^enum ([A-Za-z_][A-Za-z_0-9]*)\s*:\s*"
                             r"([A-Za-z_][A-Za-z_0-9]*)\s*;", re.MULTILINE)
    names = [match.group(1) for match in declaration.finditer(text)]
    if names != expected:
        raise ValueError("ImGui opaque-enum inventory changed: " + repr(names))
    for name in names:
        forward = re.search(r"^enum " + re.escape(name) +
                            r"\s*:\s*([A-Za-z_][A-Za-z_0-9]*)\s*;",
                            text, re.MULTILINE)
        base = forward.group(1)
        definition = re.compile(r"^enum " + re.escape(name) + r"\s*:\s*" +
                                re.escape(base) + r"\s*\{.*?^\};",
                                re.MULTILINE | re.DOTALL)
        matches = list(definition.finditer(text))
        if len(matches) != 1:
            raise ValueError("ImGui enum definition is not unique: " + name)
        full = matches[0].group()
        text = text[:matches[0].start()] + "\n" * full.count("\n") + text[matches[0].end():]
        text = text[:forward.start()] + full + text[forward.end():]
    return text


def lower_default_members(text, parser, class_names):
    """Move audited in-class defaults into every constructor of each struct."""
    for name in class_names:
        parsed = text.encode().replace(b"IMGUI_API", b"         ")
        candidates = [node for node in _nodes(parser.parse(parsed).root_node)
                      if node.type == "struct_specifier" and
                      node.child_by_field_name("name") is not None and
                      node.child_by_field_name("body") is not None and
                      node.child_by_field_name("name").text.decode() == name]
        if len(candidates) != 1:
            raise ValueError("Expected one ImGui struct: " + name)
        text, defaults = collect_member_defaults(text, name, parser, _nodes,
                                                 parse_source=parsed)
        if not defaults:
            continue
        parsed = text.encode()
        structure = next(node for node in _nodes(
            parser.parse(parsed.replace(b"IMGUI_API", b"         ")).root_node)
                         if node.type == "struct_specifier" and
                         node.child_by_field_name("name") is not None and
                         node.child_by_field_name("body") is not None and
                         node.child_by_field_name("name").text.decode() == name)
        body = structure.child_by_field_name("body")
        initializers = []
        for member in defaults:
            if member.is_array and member.value != '""':
                raise ValueError("Unaudited ImGui array default: " + name + "." + member.name)
            value = "" if member.is_array else member.value
            initializers.append(member.name + "(" + value + ")")
        constructors = []
        for child in body.named_children:
            if child.type != "function_definition":
                continue
            declarator = child.child_by_field_name("declarator")
            identifier = declarator.child_by_field_name("declarator") if declarator else None
            if identifier is not None and identifier.text.decode() == name:
                constructors.append(child)
        edits = []
        if not constructors:
            definition = "\n    " + name + "() : " + ", ".join(initializers) + " {}\n"
            edits.append((body.start_byte + 1, body.start_byte + 1, definition.encode()))
        else:
            for constructor in constructors:
                if any(node.type == "field_initializer_list" for node in constructor.named_children):
                    raise ValueError("ImGui constructor already initializes members: " + name)
                block = constructor.child_by_field_name("body")
                if block is None:
                    raise ValueError("ImGui constructor body missing: " + name)
                addition = " : " + ", ".join(initializers) + " "
                edits.append((block.start_byte, block.start_byte, addition.encode()))
        text = _replace_bytes(parsed, edits).decode()
    return text


def add_focus_scope_factory(text):
    anchor = ("struct ImGuiFocusScopeData\n{\n"
              "    ImGuiID             ID;\n"
              "    ImGuiID             WindowID;\n};")
    if text.count(anchor) != 1:
        raise ValueError("Pinned ImGui focus-scope layout changed")
    helper = ("\ninline ImGuiFocusScopeData ShipyardImGuiFocusScope(ImGuiID id, ImGuiID window)\n"
              "{\n    ImGuiFocusScopeData value;\n    value.ID = id;\n"
              "    value.WindowID = window;\n    return value;\n}\n")
    return text.replace(anchor, anchor + helper)


def lower_core_callbacks(text):
    """Give the two no-capture legacy clipboard callbacks named functions."""
    get_lambda = ("[](ImGuiContext* ctx) { return "
                  "ctx->IO.GetClipboardTextFn(ctx->IO.ClipboardUserData); }")
    set_lambda = ("[](ImGuiContext* ctx, const char* text) { return "
                  "ctx->IO.SetClipboardTextFn(ctx->IO.ClipboardUserData, text); }")
    if text.count(get_lambda) != 1 or text.count(set_lambda) != 1:
        raise ValueError("Pinned ImGui clipboard callback forms changed")
    text = text.replace(get_lambda, "ShipyardImGuiGetClipboard", 1)
    text = text.replace(set_lambda, "ShipyardImGuiSetClipboard", 1)
    anchor = '\n#include "imgui_internal.h"\n'
    if text.count(anchor) != 1:
        raise ValueError("Pinned ImGui internal include changed")
    callbacks = (
        "\nstatic const char* ShipyardImGuiGetClipboard(ImGuiContext* context)\n"
        "{ return context->IO.GetClipboardTextFn(context->IO.ClipboardUserData); }\n"
        "static void ShipyardImGuiSetClipboard(ImGuiContext* context, const char* text)\n"
        "{ context->IO.SetClipboardTextFn(context->IO.ClipboardUserData, text); }\n"
    )
    return text.replace(anchor, anchor + callbacks, 1)


def lower_focus_scope_pushes(text):
    anchors = (
        ("g.NavFocusRoute.push_back({ focus_scope_id, g.NavWindow->ID });",
         "g.NavFocusRoute.push_back(ShipyardImGuiFocusScope(focus_scope_id, g.NavWindow->ID));"),
        ("g.NavFocusRoute.push_back({ window->NavRootFocusScopeId, window->ID });",
         "g.NavFocusRoute.push_back(ShipyardImGuiFocusScope(window->NavRootFocusScopeId, window->ID));"),
    )
    for before, after in anchors:
        if text.count(before) != 1:
            raise ValueError("Pinned ImGui focus-scope push changed")
        text = text.replace(before, after, 1)
    return text
