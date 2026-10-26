#!/usr/bin/env python3
"""Snapshot import coverage guard.

A snapshot is exported by reflection: every field of an object listed in FC_REFLECT
travels to the file. The importer is written BY HAND, field by field, so a field can
be present in the snapshot and silently dropped on restore.

That is not hypothetical: the four PM one-time seed flags (pm_frozen_counters_seeded,
pm_frozen_counters_reseeded_v1, pm_active_markets_seeded, pm_oracle_gauges_seeded)
were reflected and exported but never assigned by import_dynamic_global_properties.
A node restored from such a snapshot re-seeded on its first block, and its own later
snapshots disagreed with the network (fixed by PR #184).

This script compares, for every object the snapshot plugin restores from the file:

    fields listed in FC_REFLECT  -  fields the importer assigns

and fails when something is missing. Objects imported through fc::from_variant are
skipped: reflection copies all of their fields by construction. db.modify() bodies that
repair an existing row in place are not counted as restore evidence.

Usage:
    python3 scripts/check_snapshot_import_coverage.py [--root DIR] [--verbose]
    python3 scripts/check_snapshot_import_coverage.py --selftest

Exit codes:
    0  every reflected field is assigned (or the object is copied by reflection)
    1  a reflected field is not assigned -> the snapshot silently loses it
    2  BLINDNESS: nothing parsed / a type could not be judged. "Could not check"
       must never be reported as "clean".
"""

import argparse
import os
import re
import sys

HEADER_DIRS = ("libraries/chain/include", "libraries/chain/hardfork.d")
HEADER_EXTS = (".hpp", ".h", ".hf")
IMPORTER = "plugins/snapshot/plugin.cpp"

# Type name -> fields that the importer is NOT expected to assign, with the reason.
# Both halves are mandatory: an exception without a written reason would quietly
# become the place where real gaps hide.
FIELD_EXCEPTIONS = {
    ("*", "id"): "chainbase assigns object ids via set_next_id; obj.id is never writable",
}

# Types with no FC_REFLECT: their snapshot layout is hand-written on BOTH sides, so
# reflection cannot describe it. Listing them here is a positive assertion, not an
# excuse -- any other type without reflection makes the run blind (rc 2).
NO_REFLECT_EXPORTED = {
    "proposal_object": "custom export in plugin.cpp (buffer_type + bip::flat_set need conversion)",
}

# Importer bodies that call fc::from_variant copy every reflected field; nothing to check.
REFLECTION_COPY = "fc::from_variant"

# How a db.modify() body proves it restores from the snapshot rather than repairing in place.
IMPORTER_EVIDENCE = ("v[", REFLECTION_COPY, "fc::variant(v)")

CREATE_RE = re.compile(r"db\s*\.\s*create\s*<\s*([A-Za-z_][\w:]*(?:<[^>]*>)?)\s*>")
LAMBDA_PARAM_RE = re.compile(r"\[\s*&\s*\]\s*\(\s*([A-Za-z_][\w:]*(?:<[^>]*>)?)\s*&\s*(\w+)\s*\)")

# Body-level matchers are built per lambda parameter name inside importer_bodies().


def strip_comments_and_strings(text):
    """Blank out comments and literals, keeping offsets and newlines intact."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = out[i + 1] = " "
                i += 2
        elif c == '"' or c == "'":
            quote = c
            out[i] = " "
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                if i < n and text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1
        else:
            i += 1
    return "".join(out)


def balanced(text, open_index, open_ch="(", close_ch=")"):
    """Index just past the bracket that matches text[open_index], or -1."""
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
    return -1


def parse_reflections(root):
    """Return {type: [fields]} for FC_REFLECT and FC_REFLECT_DERIVED in chain headers."""
    reflected = {}
    derived = {}
    files = 0
    for base in HEADER_DIRS:
        top = os.path.join(root, base)
        for dirpath, _dirnames, filenames in os.walk(top):
            for name in filenames:
                if not name.endswith(HEADER_EXTS):
                    continue
                path = os.path.join(dirpath, name)
                raw = strip_comments_and_strings(open(path, encoding="utf-8", errors="replace").read())
                files += 1
                for m in re.finditer(r"\bFC_REFLECT(?:_DERIVED)?\s*\(", raw):
                    text = raw[m.end() - 1:]
                    end = balanced(text, 0)
                    if end < 0:
                        continue
                    args = text[1:end - 1]
                    pieces = [p.strip() for p in split_top_level(args)]
                    if not pieces:
                        continue
                    type_name = pieces[0].strip("()").split("::")[-1].strip()
                    if not re.match(r"^[A-Za-z_]\w*$", type_name):
                        continue
                    if m.group(0).startswith("FC_REFLECT_DERIVED"):
                        if len(pieces) < 3:
                            continue
                        base_name = pieces[1].strip("()").split("::")[-1].strip()
                        fields = re.findall(r"\((\w+)\)", " ".join(pieces[2:]))
                        derived[type_name] = (base_name, fields)
                    else:
                        fields = re.findall(r"\((\w+)\)", " ".join(pieces[1:]))
                        reflected[type_name] = fields
    for type_name, (base_name, fields) in derived.items():
        inherited = reflected.get(base_name) or derived.get(base_name, (None, []))[1]
        reflected[type_name] = list(inherited) + fields
    return reflected, files


def split_top_level(text):
    """Split on commas that are not nested in parentheses."""
    parts, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return [p for p in parts if p.strip()]


def importer_bodies(root):
    """Return {type: {'fields': set, 'copy': bool, 'at': [lines]}}."""
    path = os.path.join(root, IMPORTER)
    raw = open(path, encoding="utf-8", errors="replace").read()
    clean = strip_comments_and_strings(raw)
    bodies = {}

    def add(type_name, body, offset, param="obj"):
        line = clean.count("\n", 0, offset) + 1
        entry = bodies.setdefault(type_name, {"fields": set(), "copy": False, "at": []})
        entry["at"].append(line)
        if REFLECTION_COPY in body:
            entry["copy"] = True
        prefix = r"\b%s\s*\.\s*" % re.escape(param)
        entry["fields"].update(re.findall(prefix + r"([A-Za-z_]\w*)\s*=(?!=)", body))
        entry["fields"].update(re.findall(prefix + r"([A-Za-z_]\w*)\s*[,)]", body))
        entry["fields"].update(re.findall(
            prefix + r"([A-Za-z_]\w*)\s*\.\s*"
            r"(?:resize|assign|reserve|clear|push_back|emplace_back|emplace|insert|append|push)\s*\(",
            body))

    for cre in CREATE_RE.finditer(clean):
        brace = clean.find("{", cre.end())
        if brace < 0:
            continue
        end = balanced(clean, brace, "{", "}")
        if end < 0:
            continue
        lam = LAMBDA_PARAM_RE.search(clean[cre.end():brace])
        param = lam.group(2) if lam else "obj"
        add(cre.group(1), clean[brace:end], cre.start(), param)

    for mod in re.finditer(r"db\s*\.\s*modify\s*\(", clean):
        par = clean.find("(", mod.end() - 1)
        end = balanced(clean, par)
        if end < 0:
            continue
        call = clean[par:end]
        # An importer reads the snapshot variant; the other modifies are in-place repairs
        # (LIB promotion, ballot reconciliation, self-healing) and must not count as
        # evidence that a field is restored.
        if not any(ev in call for ev in IMPORTER_EVIDENCE):
            continue
        lam = LAMBDA_PARAM_RE.search(call)
        if not lam:
            continue
        brace = call.find("{", lam.end())
        if brace < 0:
            continue
        body_end = balanced(call, brace, "{", "}")
        if body_end < 0:
            continue
        add(lam.group(1), call[brace:body_end], mod.start(), lam.group(2))

    return bodies


def check(root, verbose=False):
    reflected, header_files = parse_reflections(root)
    if header_files == 0:
        print("СЛЕПОТА: заголовков chain не прочитано ни одного")
        return 2
    if not reflected:
        print("СЛЕПОТА: ни одного FC_REFLECT не разобрано")
        return 2
    bodies = importer_bodies(root)
    if not bodies:
        print("СЛЕПОТА: в %s не найдено ни одного create/modify" % IMPORTER)
        return 2

    problems, judged, copies, out_of_scope, unreflected = [], 0, [], [], []
    for type_name in sorted(bodies):
        entry = bodies[type_name]
        if entry["copy"]:
            copies.append(type_name)
            continue
        fields = reflected.get(type_name)
        if fields is None:
            if type_name in NO_REFLECT_EXPORTED:
                out_of_scope.append(type_name)
            else:
                unreflected.append((type_name, entry["at"]))
            continue
        judged += 1
        missing = []
        for field in fields:
            if field in entry["fields"]:
                continue
            reason = FIELD_EXCEPTIONS.get((type_name, field)) or FIELD_EXCEPTIONS.get(("*", field))
            if reason:
                continue
            missing.append(field)
        if missing:
            problems.append((type_name, missing, entry["at"]))
        elif verbose:
            print("%s: %d/%d полей присвоено" % (type_name, len(fields), len(fields)))

    print("объектов в импортёре: %d (сверено поле за полем: %d, копия по рефлексии: %d, "
          "вне охвата без FC_REFLECT: %d)"
          % (len(bodies), judged, len(copies), len(out_of_scope)))
    for type_name, missing, at in problems:
        print("НАХОДКА %s (plugin.cpp:%s): не присвоено %s"
              % (type_name, ",".join(str(x) for x in at), ", ".join(missing)))
    for type_name, at in unreflected:
        print("СЛЕПОТА %s (plugin.cpp:%s): FC_REFLECT не найден и в NO_REFLECT_EXPORTED нет — "
              "судить нечем" % (type_name, ",".join(str(x) for x in at)))
    if unreflected:
        return 2
    return 1 if problems else 0


# ---------------------------------------------------------------------------
# Self-test: proves the guard can redden and can tell blindness from cleanliness.
# ---------------------------------------------------------------------------

HEADER_FIXTURE = """
struct plain_object { int a = 0; int b = 0; };
FC_REFLECT((graphene::chain::plain_object), (id)(a)(b))
struct copy_object { int a = 0; };
FC_REFLECT((graphene::chain::copy_object), (id)(a))
struct derived_object : public plain_object { int c = 0; };
FC_REFLECT_DERIVED((graphene::chain::derived_object), ((graphene::chain::plain_object)), (c))
struct set_object { int a = 0; };
FC_REFLECT((graphene::chain::set_object), (id)(a))
struct hand_object { int a = 0; };
"""

PLUGIN_COVERED = """
db.create<plain_object>([&](plain_object& obj) {
    obj.a = v["a"].as_int64();
    obj.b = v["b"].as_int64();
});
db.create<copy_object>([&](copy_object& obj) { fc::from_variant(v, obj); });
db.create<derived_object>([&](derived_object& obj) {
    obj.a = 1; obj.b = 2; obj.c = 3;
});
db.create<set_object>([&](set_object& obj) { obj.a.insert(1); });
"""

PLUGIN_MISSING = PLUGIN_COVERED.replace('    obj.b = v["b"].as_int64();\n', "")

PLUGIN_UNKNOWN = PLUGIN_COVERED + 'db.create<ghost_object>([&](ghost_object& obj) { obj.x = 1; });\n'

# proposal_object is the real hand-written pair: listed, therefore not blind.
PLUGIN_HAND_LISTED = PLUGIN_COVERED + 'db.create<proposal_object>([&](proposal_object& obj) { obj.x = 1; });\n'


def selftest():
    import tempfile
    arms = []

    def fixture(plugin_text):
        tmp = tempfile.mkdtemp()
        os.makedirs(os.path.join(tmp, "libraries/chain/include"))
        os.makedirs(os.path.join(tmp, "plugins/snapshot"))
        with open(os.path.join(tmp, "libraries/chain/include/fixture.hpp"), "w") as fh:
            fh.write(HEADER_FIXTURE)
        with open(os.path.join(tmp, "plugins/snapshot/plugin.cpp"), "w") as fh:
            fh.write(plugin_text)
        return tmp

    arms.append(("A покрыто -> 0", fixture(PLUGIN_COVERED), 0))
    arms.append(("B пропуст поле -> 1", fixture(PLUGIN_MISSING), 1))
    arms.append(("C неизвестный тип -> 2", fixture(PLUGIN_UNKNOWN), 2))
    arms.append(("E вне охвата по списку -> 0", fixture(PLUGIN_HAND_LISTED), 0))
    empty = fixture(PLUGIN_COVERED)
    open(os.path.join(empty, "plugins/snapshot/plugin.cpp"), "w").close()
    arms.append(("D пустой импортёр -> 2", empty, 2))

    failed = 0
    for name, root, want in arms:
        import io
        import contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            got = check(root)
        mark = "ок" if got == want else "КРАСНОЕ"
        print("  %s %s (rc=%d, ждал %d)" % (mark, name, got, want))
        if got != want:
            failed += 1
    print("самотест: %s" % ("все плечи зелёные" if failed == 0 else "ЕСТЬ ПРОВАЛ"))
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    return check(os.path.abspath(args.root), args.verbose)


if __name__ == "__main__":
    sys.exit(main())
