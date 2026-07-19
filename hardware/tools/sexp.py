"""Minimal s-expression parser/serializer for KiCad files."""


def parse(text):
    """Parse s-expression text into nested lists of str/atoms."""
    tokens = _tokenize(text)
    pos = 0

    def read():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok == "(":
            lst = []
            while tokens[pos] != ")":
                lst.append(read())
            pos += 1
            return lst
        return tok

    result = read()
    return result


def _tokenize(text):
    tokens = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "()":
            tokens.append(c)
            i += 1
        elif c == '"':
            j = i + 1
            buf = []
            while text[j] != '"':
                if text[j] == "\\":
                    buf.append(text[j + 1])
                    j += 2
                else:
                    buf.append(text[j])
                    j += 1
            tokens.append(Quoted("".join(buf)))
            i = j + 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in "()":
                j += 1
            tokens.append(text[i:j])
            i = j
    return tokens


class Quoted(str):
    """String that was quoted in source and must be re-quoted on output."""


def dump(node, indent=0):
    if isinstance(node, Quoted):
        return '"%s"' % node.replace("\\", "\\\\").replace('"', '\\"')
    if isinstance(node, str):
        return node
    parts = [dump(x) for x in node]
    line = "(" + " ".join(parts) + ")"
    if len(line) <= 100:
        return line
    pad = "  " * (indent + 1)
    out = "(" + dump(node[0])
    for x in node[1:]:
        out += "\n" + pad + dump(x, indent + 1)
    out += ")"
    return out


def find_all(node, tag):
    return [x for x in node if isinstance(x, list) and x and x[0] == tag]


def find(node, tag):
    hits = find_all(node, tag)
    return hits[0] if hits else None
