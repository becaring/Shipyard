"""Native-header binding for the selected portable libpng implementation."""


def adapt(name, text, *, version):
    if version != "1.6.58":
        raise ValueError("Unreviewed libpng version")
    if name == "pngpriv.h":
        before = "#if defined(_WIN32) || defined(__WIN32__) || defined(__NT__)\n#  include <windows.h>\n#endif"
        if text.count(before) != 1:
            raise ValueError("libpng native platform include changed")
        text = text.replace(
            before,
            "#if defined(_XBOX)\n#  include <xtl.h>\n#elif defined(_WIN32) || defined(__WIN32__) || defined(__NT__)\n#  include <windows.h>\n#endif",
        )
    return text
