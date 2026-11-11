#!/usr/bin/env python3
"""Offline structural verification for the launcher's Java sources.

There is no JDK in the environment these changes are made in, so the Java half of the performance work cannot be
compiled. This tool recovers the checks that a compiler would catch first, without needing one:

  * brace/paren/bracket balance per file, ignoring comments, string and char literals,
  * every type used in a declaration position is resolvable: imported, declared in the same file (including inner
    classes), in the same package, an auto-imported java.lang type, or a fully qualified name,
  * no field is declared `final` and then assigned outside its declaration,
  * no two methods in one class share a signature,
  * every override/implementation the file claims (`@Override`) has a matching method in a class it extends or an
    interface it implements, when that type is one of this repository's own sources.

It is deliberately conservative: anything it cannot prove is skipped rather than reported, because a false positive
here would block a correct change. What it does catch is the class of mistake that actually happens when Java is edited
by hand without a compiler - a missing import, a stale field modifier, a duplicated method.

Run: python3 tools/test_java_sources.py [--verbose]
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Overridable so the checker's own self-test can run it against a mutated copy of the sources.
SOURCE_ROOT = Path(os.environ.get("PROTODROID_JAVA_ROOT") or ROOT / "android/java").resolve()

# Auto-imported and universal types, plus the Android/iOS shims that are always available in this project.
# java.lang is imported implicitly, so a plain reference to any of these needs no import clause.
JAVA_LANG = {
    "Object", "String", "Integer", "Long", "Float", "Double", "Boolean", "Byte", "Short", "Character", "Number",
    "Math", "StrictMath", "System", "Thread", "ThreadLocal", "Runnable", "Exception", "RuntimeException", "Error",
    "Throwable", "Class", "ClassLoader", "StringBuilder", "StringBuffer", "Iterable", "Comparable", "CharSequence",
    "Override", "Deprecated", "Void", "Process", "Runtime", "Package", "Enum", "Cloneable", "AutoCloseable",
    "AssertionError", "IllegalArgumentException", "IllegalStateException", "IllegalAccessException",
    "InterruptedException", "IndexOutOfBoundsException", "ArrayIndexOutOfBoundsException",
    "NumberFormatException", "NullPointerException", "UnsupportedOperationException", "SecurityException",
    "ClassNotFoundException", "NoSuchMethodException", "NoSuchFieldException", "ReflectiveOperationException",
    "InstantiationException", "OutOfMemoryError", "StackOverflowError", "ArithmeticException",
    "StringIndexOutOfBoundsException", "ClassCastException", "NegativeArraySizeException", "Appendable",
    "Readable", "Comparable", "FunctionalInterface", "SafeVarargs", "SuppressWarnings", "Documented",
    "UnsatisfiedLinkError", "LinkageError", "NoClassDefFoundError", "ExceptionInInitializerError",
    "Inherited", "Retention", "Target", "Repeatable", "Native", "StrictMath", "Integer",
}
# Not defined anywhere in the sources but supplied by the platform/other modules the launcher links against.
EXTERNAL = {"R"}  # generated resource class

PRIMITIVES = {"void", "boolean", "byte", "char", "short", "int", "long", "float", "double", "var"}


def strip_literals(source: str) -> str:
    """Blanks out comments and literals while preserving offsets and newlines."""
    out: list[str] = []
    index = 0
    length = len(source)
    while index < length:
        ch = source[index]
        two = source[index:index + 2]
        if two == "//":
            end = source.find("\n", index)
            end = length if end < 0 else end
            out.append(" " * (end - index))
            index = end
        elif two == "/*":
            end = source.find("*/", index + 2)
            end = length if end < 0 else end + 2
            out.append("".join("\n" if c == "\n" else " " for c in source[index:end]))
            index = end
        elif ch == '"':
            end = index + 1
            while end < length and source[end] != '"':
                end += 2 if source[end] == "\\" else 1
            end = min(end + 1, length)
            out.append('"' + " " * (end - index - 2) + '"' if end - index >= 2 else '"')
            index = end
        elif ch == "'":
            end = index + 1
            while end < length and source[end] != "'":
                end += 2 if source[end] == "\\" else 1
            end = min(end + 1, length)
            out.append(" " * (end - index))
            index = end
        else:
            out.append(ch)
            index += 1
    return "".join(out)


@dataclass
class JavaFile:
    path: Path
    raw: str
    code: str            # literals and comments blanked
    package: str = ""
    imports: set[str] = field(default_factory=set)
    types: set[str] = field(default_factory=set)      # declared in this file
    supertypes: dict[str, list[str]] = field(default_factory=dict)  # inner type -> [extends/implements]

    @property
    def name(self) -> str:
        return self.path.stem


def load(path: Path) -> JavaFile:
    raw = path.read_text(encoding="utf-8", errors="replace")
    java = JavaFile(path=path, raw=raw, code=strip_literals(raw))
    match = re.search(r"^\s*package\s+([\w.]+)\s*;", java.code, re.MULTILINE)
    java.package = match.group(1) if match else ""
    for imported in re.finditer(r"^\s*import\s+(?:static\s+)?([\w.*]+)\s*;", java.code, re.MULTILINE):
        java.imports.add(imported.group(1).split(".")[-1])
    # Declared types: the top-level class, interfaces and enums, plus nested ones.
    for declared in re.finditer(
            r"\b(?:class|interface|enum|record)\s+([A-Z][\w]*)", java.code):
        java.types.add(declared.group(1))
    # `extends A implements B, C` per declared type: recorded loosely, used only to check @Override.
    for declared in re.finditer(
            r"\b(?:class|interface|enum)\s+([A-Z][\w]*)[^{]*?(?:extends\s+([\w.<>, ]+?))?(?:implements\s+([\w.<>, ]+?))?\s*\{",
            java.code):
        parents = [p.split("<")[0].strip() for part in (declared.group(2), declared.group(3)) if part
                   for p in part.split(",")]
        java.supertypes.setdefault(declared.group(1), []).extend(p for p in parents if p)
    return java


def balance(java: JavaFile) -> list[str]:
    problems = []
    for opener, closer, label in (("{", "}", "braces"), ("(", ")", "parentheses"), ("[", "]", "brackets")):
        depth = 0
        for index, ch in enumerate(java.code):
            if ch == opener:
                depth += 1
            elif ch == closer:
                depth -= 1
                if depth < 0:
                    line = java.code.count("\n", 0, index) + 1
                    problems.append(f"{java.path.name}:{line}: unmatched {closer} ({label})")
                    depth = 0
        if depth:
            problems.append(f"{java.path.name}: {depth} unclosed {opener} ({label})")
    return problems


TYPE_DECL = re.compile(
    r"(?<![\w.])(?:(?:final|static|private|public|protected|volatile|transient|synchronized)\s+)*"
    r"(?:(?:final)\s+)?(?P<type>[A-Z][\w.]*)"
    r"(?:\s*<[^;{}]*>)?(?:\s*\[\s*\])*\s+"
    r"(?P<name>[a-z_][\w]*)\s*(?:[=;,)]|\()")


def referenced_types(java: JavaFile) -> list[tuple[str, int]]:
    found: list[tuple[str, int]] = []
    for match in TYPE_DECL.finditer(java.code):
        type_name = match.group("type").split(".")[0]
        line = java.code.count("\n", 0, match.start()) + 1
        found.append((type_name, line))
    for match in re.finditer(r"\bnew\s+([A-Z][\w]*)\s*[<(]", java.code):
        found.append((match.group(1), java.code.count("\n", 0, match.start()) + 1))
    for match in re.finditer(r"\b(?:extends|implements)\s+([\w.]+(?:<[^>;{}]*>)?(?:\s*,\s*[\w.]+(?:<[^>;{}]*>)?)*)\s*\{",
                             java.code):
        for part in match.group(1).split(","):
            name = part.split("<")[0].strip().split(".")[0].strip()
            if name and name[0].isupper() and " " not in name:
                found.append((name, java.code.count("\n", 0, match.start()) + 1))
    return found


def unresolved_static_access(java: JavaFile, siblings: set[str]) -> list[str]:
    """`Type.FIELD` / `Type.method(` where Type is not imported or declared: javac's 'package Type does not exist'."""
    problems = []
    known = JAVA_LANG | EXTERNAL | java.types | java.imports | siblings
    for match in re.finditer(r"(?<![\w.])([A-Z][\w]*)\s*\.\s*([A-Za-z_][\w]*)\s*(?:\(|that\b|[.,;)])", java.code):
        name = match.group(1)
        # All-caps identifiers are constant fields (BY_CODE.length, TAG.name()), not types; a type name has at least
        # one lower-case letter.
        if name in known or name in PRIMITIVES or re.fullmatch(r"[A-Z]", name) or not re.search(r"[a-z]", name):
            continue
        line = java.code.count("\n", 0, match.start()) + 1
        problems.append(f"{java.path.name}:{line}: static access on unknown type {name} "
                        f"(no import, not in this package) - javac says 'package {name} does not exist'")
    return problems


def unresolved(java: JavaFile, siblings: set[str]) -> list[str]:
    problems = []
    for type_name, line in referenced_types(java):
        if type_name in PRIMITIVES or type_name in JAVA_LANG or type_name in EXTERNAL:
            continue
        if type_name in java.imports or type_name in java.types or type_name in siblings:
            continue
        if re.match(r"^[A-Z]$", type_name):  # generic parameter
            continue
        problems.append(f"{java.path.name}:{line}: cannot resolve type {type_name} (no import, not in this package)")
    return problems


# A `final` field assigned more than once is a compile error - but only when it already has an initializer, because a
# `final` field without one is legitimately assigned by each constructor. This catches the mistake that actually
# happens by hand: keeping `final` on a field a later refactor started reassigning.
FIELD = re.compile(
    r"(?P<mods>(?:\b(?:private|protected|public|static|volatile|transient)\s+)*)final\s+"
    r"(?P<type>[A-Za-z_][\w.]*(?:\s*<[^;{}]*>)?(?:\s*\[\s*\])*)\s+(?P<name>[a-z_][\w]*)\s*=")


def final_reassigned(java: JavaFile) -> list[str]:
    problems = []
    for match in FIELD.finditer(java.code):
        line_start = java.code.rfind("\n", 0, match.start()) + 1
        indent = len(java.code[line_start:match.start()]) - len(java.code[line_start:match.start()].lstrip())
        if indent > 4:
            continue  # a local variable, not a field: locals are legally final and are simply re-declared
        name = match.group("name")
        # Assignments to the field outside its declaration: `name =`, `name +=`, `this.name =`.
        for assignment in re.finditer(rf"(?<![\w.])(?:this\.)?{re.escape(name)}\s*(?:=[^=]|\+=|-=|\*=|/=|\|=|&=)", java.code):
            if assignment.start() <= match.end():
                continue
            line = java.code.count("\n", 0, assignment.start()) + 1
            problems.append(f"{java.path.name}:{line}: field {name} is final but assigned again")
            break
    return problems


METHOD = re.compile(
    r"^[ \t]*(?:@\w+(?:\([^)]*\))?\s+)*"
    r"(?:(?:public|private|protected|static|final|synchronized|native|abstract|default)\s+)*"
    r"(?:<[^>]+>\s*)?(?P<ret>[A-Za-z_][\w.<>\[\], ]*?)\s+(?P<name>[a-zA-Z_]\w*)\s*\((?P<args>[^)]*)\)\s*(?:throws [\w., ]+)?\{",
    re.MULTILINE)


def method_signature(args: str) -> str:
    """Normalised parameter types, so overloads are distinguished and true duplicates are not."""
    text = args.strip()
    if not text:
        return ""
    parts: list[str] = []
    depth = 0
    current = ""
    for ch in text:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += ch
    parts.append(current)
    types = []
    for part in parts:
        tokens = [t for t in re.split(r"\s+", part.strip()) if t and t != "final"]
        if not tokens:
            continue
        # `Type name` -> Type; drop annotations and generic arguments for comparison.
        types.append(re.sub(r"<.*", "", tokens[0]) + ("[]" if "[]" in part else ""))
    return ",".join(types)


OWNER_PATTERNS = (
    re.compile(r"\bnew\s+([\w.]+)\s*\([^;{}]*\)\s*\{"),      # anonymous class
    re.compile(r"\b(?:class|interface|enum)\s+([\w]+)"),          # nested type
    re.compile(r"\b([a-z_]\w*)\s*=\s*new\s+[\w.]+\s*\("),     # field initialised with an anonymous class
)


def enclosing_owner(code: str, position: int) -> str:
    """Names the innermost enclosing class or anonymous class, so overloads in different bodies are not compared."""
    best, best_at = "", -1
    for index, pattern in enumerate(OWNER_PATTERNS):
        for match in pattern.finditer(code, 0, position):
            if match.end() > best_at:
                # Anonymous bodies get their position folded in: two `new Runnable() {` bodies are different classes.
                suffix = f"@{match.start()}" if index != 1 else ""
                best, best_at = match.group(1).rsplit(".", 1)[-1] + suffix, match.end()
    return best


def duplicate_methods(java: JavaFile) -> list[str]:
    problems = []
    seen: dict[tuple[int, str, str], int] = {}
    for match in METHOD.finditer(java.code):
        name, arity = match.group("name"), method_signature(match.group("args"))
        if name in ("if", "for", "while", "switch", "catch", "synchronized", "new"):
            continue
        line = java.code.count("\n", 0, match.start()) + 1
        owner = enclosing_owner(java.code, match.start())
        key = (owner, name, arity)
        if key in seen:
            problems.append(f"{java.path.name}:{line}: method {name}({arity}) already defined on line {seen[key]}")
        else:
            seen[key] = line
    return problems


NUMERIC = {"long", "int", "short", "byte", "float", "double", "Long", "Integer", "Float", "Double"}


def split_args(text: str) -> list[str]:
    parts, depth, current = [], 0, ""
    for ch in text:
        if ch in "([{<":
            depth += 1
        elif ch in ")]}>":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += ch
    if current.strip():
        parts.append(current)
    return parts


def string_into_numeric_parameter(java: JavaFile) -> list[str]:
    """`f("text" + x)` where every overload of `f` takes a number: javac's 'String cannot be converted to long'."""
    problems = []
    declarations: dict[str, list[list[str]]] = {}
    for match in METHOD.finditer(java.code):
        name = match.group("name")
        if name in ("if", "for", "while", "switch", "catch", "synchronized") or match.group("ret") in ("new", "return"):
            continue
        params = []
        for part in split_args(match.group("args")):
            tokens = [t for t in re.split(r"\s+", part.strip()) if t and t not in ("final",)]
            if len(tokens) >= 2:
                params.append(re.sub(r"<.*", "", tokens[0]) + ("[]" if "[]" in part else ""))
        declarations.setdefault(name, []).append(params)
    for name, overloads in declarations.items():
        if any(any(not t or t in ("String", "CharSequence", "Object") for t in params) for params in overloads):
            continue  # an overload that takes text exists: no conclusion
        arities = {len(params) for params in overloads if params}
        for call in re.finditer(rf"(?<![\w.]){re.escape(name)}\s*\(", java.code):
            body_start = call.end()
            depth, index = 1, body_start
            while index < len(java.code) and depth:
                if java.code[index] == "(":
                    depth += 1
                elif java.code[index] == ")":
                    depth -= 1
                index += 1
            args = split_args(java.code[body_start:index - 1])
            if len(args) not in arities:
                continue
            for position, arg in enumerate(args):
                if "\"" not in arg:
                    continue
                if all(position < len(params) and params[position] in NUMERIC for params in overloads if params):
                    line = java.code.count("\n", 0, call.start()) + 1
                    problems.append(f"{java.path.name}:{line}: argument {position + 1} of {name}(...) is a String but "
                                    f"the parameter is numeric")
                    break
    return problems


def check_overrides(java: JavaFile, by_name: dict[str, JavaFile]) -> list[str]:
    problems = []
    defined = {match.group("name") for match in METHOD.finditer(java.code)}
    for match in re.finditer(r"@Override\s+(?:public|protected|private|\s)*[\w<>\[\], ]+\s+(\w+)\s*\(", java.code):
        name = match.group(1)
        if name not in defined:  # the annotation must sit on a definition; covered by balance/duplicate checks
            continue
        parents = [p for p in sum(java.supertypes.values(), []) if p in by_name and p != java.name]
        if not parents:
            continue  # extends something outside this repository: cannot verify
        inherited = set()
        for parent in parents:
            inherited |= {m.group("name") for m in METHOD.finditer(by_name[parent].code)}
        if name not in inherited:
            line = java.code.count("\n", 0, match.start()) + 1
            problems.append(f"{java.path.name}:{line}: @Override {name} does not exist in {', '.join(parents)}")
    return problems


def self_test() -> list[str]:
    """Reintroduces the two mistakes javac actually caught here and requires the checker to report them."""
    import shutil
    import tempfile

    failures = []
    with tempfile.TemporaryDirectory(prefix="protodroid-java-") as temp:
        target = Path(temp) / "java"
        shutil.copytree(SOURCE_ROOT, target)
        touch = target / "com" / "protodroid" / "TouchControls.java"
        original = touch.read_text(encoding="utf-8")
        broken = original.replace(
            "cached(stickKey(0, false), stickR + 12 * density",
            'cached("stick:" + stickR, stickR + 12 * density')
        if broken == original:
            failures.append("self-test: could not find the stick cache call to break")
        touch.write_text(broken, encoding="utf-8")
        for need, label in (('"stick:" + stickR', "string into a numeric parameter"),):
            if need not in broken:
                failures.append(f"self-test: the mutation for {label} did not apply")
            else:
                found = subprocess.run(
                    [sys.executable, str(Path(__file__).resolve()), "--quiet"],
                    capture_output=True, text=True,
                    env={**os.environ, "PROTODROID_JAVA_ROOT": str(target)})
                if found.returncode == 0:
                    failures.append(f"self-test: the checker did not report {label}")
        proto = target / "com" / "protodroid" / "ProtoActivity.java"
        text = proto.read_text(encoding="utf-8").replace("import android.os.Build;\n", "")
        text = text.replace("Build.VERSION.SDK_INT < 33", "android.os.Build.VERSION.SDK_INT < 33")
        proto.write_text(text, encoding="utf-8")
        found = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--quiet"],
                               capture_output=True, text=True,
                               env={**os.environ, "PROTODROID_JAVA_ROOT": str(target)})
        if found.returncode == 0:
            failures.append("self-test: the checker did not report an unimported type")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--quiet", action="store_true", help="only report problems (used by the self-test)")
    parser.add_argument("--self-test", action="store_true", help="prove the checker catches the mistakes javac caught")
    args = parser.parse_args()

    if args.self_test:
        failures = self_test()
        for failure in failures:
            print(f"  - {failure}")
        if failures:
            print(f"java checker self-test: {len(failures)} failure(s)")
            return 1
        print("java checker self-test passed: it reports a string in a numeric parameter and an unimported type")
        return 0

    paths = sorted(SOURCE_ROOT.rglob("*.java"))
    if not paths:
        print(f"no Java sources under {SOURCE_ROOT}")
        return 1
    files = [load(path) for path in paths]
    by_name = {java.name: java for java in files}
    siblings = set(by_name)

    problems: list[str] = []
    for java in files:
        problems += balance(java)
        problems += unresolved(java, siblings)
        problems += unresolved_static_access(java, siblings)
        problems += final_reassigned(java)
        problems += duplicate_methods(java)
        problems += string_into_numeric_parameter(java)
        problems += check_overrides(java, by_name)

    if (args.verbose or problems) and not args.quiet:
        for java in files:
            defs = ", ".join(sorted(java.types))
            print(f"{java.path.relative_to(ROOT)}: package {java.package or '(default)'}; types {defs}")
    if problems:
        print(f"\n{len(problems)} problem(s) in {len(files)} Java source(s):")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    if not args.quiet:
        print(f"java sources ok: {len(files)} files, {sum(len(j.types) for j in files)} types, "
              f"no structural problems")
    return 0


if __name__ == "__main__":
    sys.exit(main())
