"""Reflected layouts in: local types from a layout-export file, and `Class *this` on methods."""
import json
import re

from .database import _auto_name
from .labels import _database_build, best, load, made_up, of_build

_C_UNSAFE = re.compile(r'[^A-Za-z0-9_]')
_KEYWORDS = {'auto', 'bool', 'break', 'case', 'char', 'class', 'const', 'continue', 'default', 'delete', 'do',
             'double', 'else', 'enum', 'extern', 'false', 'float', 'for', 'goto', 'if', 'int', 'long', 'new',
             'private', 'protected', 'public', 'register', 'return', 'short', 'signed', 'sizeof', 'static',
             'struct', 'switch', 'template', 'this', 'true', 'typedef', 'union', 'unsigned', 'virtual', 'void',
             'volatile', 'while'}
# Property kinds with a fixed C type: (type, natural size).
_SCALARS = {
    'FloatProperty': ('float', 4), 'DoubleProperty': ('double', 8), 'BoolProperty': ('unsigned __int8', 1),
    'Int8Property': ('__int8', 1), 'Int16Property': ('__int16', 2), 'IntProperty': ('__int32', 4),
    'Int64Property': ('__int64', 8), 'ByteProperty': ('unsigned __int8', 1),
    'UInt16Property': ('unsigned __int16', 2), 'UInt32Property': ('unsigned __int32', 4),
    'UInt64Property': ('unsigned __int64', 8),
    'NameProperty': ('struct FName', 8), 'StrProperty': ('struct FString', 16),
    'WeakObjectProperty': ('struct FWeakObjectPtr', 8), 'InterfaceProperty': ('struct FScriptInterface', 16),
}
_POINTERS = ('ObjectProperty', 'ClassProperty', 'ObjectPtrProperty', 'ClassPtrProperty')
# Engine types the properties above use. layout-export names types without their C++ prefix, so these
# do not meet an exported name unless a file holds one of that name (then the file's is used).
_ENGINE_TYPES = {
    'FName': 'struct FName { __int32 ComparisonIndex; __int32 Number; };',
    'FString': 'struct FString { unsigned __int16 *Data; __int32 Num; __int32 Max; };',
    'TArray': 'struct TArray { void *Data; __int32 Num; __int32 Max; };',
    'FWeakObjectPtr': 'struct FWeakObjectPtr { __int32 ObjectIndex; __int32 ObjectSerialNumber; };',
    'FScriptInterface': 'struct FScriptInterface { void *Object; void *Interface; };',
}
_INTS = {1: 'unsigned __int8', 2: 'unsigned __int16', 4: 'unsigned __int32', 8: 'unsigned __int64'}


def c_name(name):
    """A C identifier for `name`: other characters '_', a leading digit or a keyword gets a '_'."""
    out = _C_UNSAFE.sub('_', name) or '_'
    return '_' + out if out[0].isdigit() or out in _KEYWORDS else out


def _order(classes):
    """Type names so each comes after the types it holds by value (its super, its struct fields)."""
    done, out = set(), []

    def visit(name, path):
        if name in done or name not in classes or name in path:
            return
        path = path | {name}
        held = [classes[name].get('base')] + [f.get('struct') for f in classes[name]['fields']]
        for other in held:
            if other:
                visit(other, path)
        done.add(name)
        out.append(name)

    for name in classes:
        visit(name, frozenset())
    return out


def _ctype(prop, classes, enums, arrays):
    """(C type, natural size, None) for one property (a field, or a container's element), or
    (None, 0, why). A typed TArray's declaration is added to `arrays` {name: text}."""
    kind = prop.get('type', '')
    if kind in _POINTERS:
        target = prop.get('class')
        return (f'struct {c_name(target)} *' if target in classes else 'void *'), 8, None
    if prop.get('enum') in enums:
        return f'enum {c_name(prop["enum"])}', enums[prop['enum']].get('size') or 1, None
    if kind == 'StructProperty':
        name = prop.get('struct')
        if name not in classes or not classes[name].get('size'):
            return None, 0, f'struct {name} not in the file'
        return f'struct {c_name(name)}', classes[name]['size'], None
    if kind == 'ArrayProperty':
        element, size, why = _ctype(prop.get('element') or {}, classes, enums, arrays)
        if not element or size != (prop.get('element') or {}).get('size'):
            return 'struct TArray', 16, None
        name = 'TArray_' + c_name(element.replace('struct ', '').replace('enum ', '').replace('*', 'ptr').strip())
        arrays[name] = f'struct {name} {{ {element}{"" if element.endswith("*") else " "}*Data; __int32 Num; __int32 Max; }};'
        return f'struct {name}', 16, None
    if kind == 'EnumProperty' and prop.get('size') in _INTS:  # Its enum not in the file: the integer.
        return _INTS[prop['size']], prop['size'], None
    known = _SCALARS.get(kind)
    if known:
        return known[0], known[1], None
    return None, 0, f'{kind or "no type"}: no C type'


def _enum_values(name, enum, taken):
    """An enum's member declarations (names unique across enums, values that fit its width) and the
    values left out."""
    width = enum.get('size') or 1
    low, high = -(1 << (8 * width - 1)), (1 << (8 * width)) - 1
    values, dropped = [], []
    for value_name, value in enum['values']:
        member = c_name(value_name)
        if member in taken:
            member = c_name(f'{name}__{value_name}')
        if member in taken or not low <= value <= high:
            dropped.append(f'{name}.{value_name}: {"name taken" if member in taken else f"{value} does not fit {width} bytes"}')
            continue
        taken.add(member)
        values.append(f'{member} = {value if value < 0 else hex(value)}')
    return values, dropped


def declarations(classes, enums):
    """C declarations for the types and enums of a layout file (as `layouts` reads it), in one
    `#pragma pack(1)` block so every member sits at its reflected offset, and a report.

    A type's super, when the file holds it and its size fits before the first field, is its base
    class (`struct Pawn : Actor`), so the decompiler shows inherited fields as the type's own; else
    that part is padding. Fields follow at their offsets, typed by property kind: numbers, bool as
    uint8, FName, FString, FWeakObjectPtr and FScriptInterface as engine structs, a TArray as
    `TArray_<element>` {Data, Num, Max} (plain `TArray` when its element has no C type), object
    pointers as pointers to the class's type when the file holds it (else `void *`), a struct by
    value when the file holds it, an enum when the file holds it; a static array as an array of
    its element. Fields sharing a byte (bitfield bools) become one member named after all of them.
    Gaps are byte arrays `pad_<offset>`, and every type is padded to its size. A field with no C
    type is a byte array of its size; a field overlapping the one before is left out.
    Returns (text, report): {'types', 'enums', 'arrays': names declared, 'untyped': ['Type.Field:
    why'], 'skipped': ['Type.Field: why', 'Type: why', 'Enum.Value: why']}."""
    report = {'types': [], 'enums': [], 'arrays': [], 'untyped': [], 'skipped': []}
    lines = ['#pragma pack(push, 1)']
    lines += [text for name, text in _ENGINE_TYPES.items() if name not in classes]
    lines += [f'struct {c_name(name)};' for name in classes]
    taken = set()  # Enum members share one namespace.
    for name, enum in enums.items():
        values, dropped = _enum_values(name, enum, taken)
        lines.append(f'enum {c_name(name)} : {_INTS.get(enum.get("size") or 1, "unsigned __int8")} '
                     f'{{ {", ".join(values)} }};')
        report['enums'].append(name)
        report['skipped'] += dropped
    arrays, sizes, bodies = {}, {}, []
    for name in _order(classes):
        cls = classes[name]
        fields = sorted(cls['fields'], key=lambda f: f['offset'])
        members, used, at, head = [], set(), 0, f'struct {c_name(name)}'
        base = cls.get('base')
        if base:
            first = fields[0]['offset'] if fields else cls.get('size')
            if base not in sizes:
                report['skipped'].append(f'{name}: super {base} not in the file, padded')
            elif first is not None and sizes[base] > first:
                report['skipped'].append(f'{name}: super {base} size {sizes[base]:#x} past the first field, padded')
            else:
                head += f' : {c_name(base)}'
                at = sizes[base]
        index = 0
        while index < len(fields):
            field = fields[index]
            group = [field]
            while index + len(group) < len(fields) and fields[index + len(group)]['offset'] == field['offset']:
                group.append(fields[index + len(group)])
            index += len(group)
            offset, size = field['offset'], max(f.get('size', 0) for f in group)
            if offset < at:
                report['skipped'] += [f'{name}.{f["field"]}: overlaps the member before (at {at:#x})' for f in group]
                continue
            if offset > at:
                members.append(f'unsigned __int8 pad_{at:x}[{offset - at:#x}];')
            member = c_name('__'.join(f['field'] for f in group))
            member = member if len(member) <= 96 else member[:96].rstrip('_')  # No dangling separator.
            while member in used:
                member += '_'
            used.add(member)
            if len(group) > 1:
                typed, natural, why = (_INTS.get(size), size, None) if size in _INTS else \
                    (None, 0, f'{len(group)} fields share the offset')
            else:
                typed, natural, why = _ctype(field, classes, enums, arrays)
            if typed and natural and size % natural == 0 and size:
                count = size // natural
                spaced = typed if typed.endswith('*') else typed + ' '
                members.append(f'{spaced}{member}{f"[{count}]" if count > 1 else ""};')
            elif size > 0:
                members.append(f'unsigned __int8 {member}[{size:#x}];')
                report['untyped'].append(f'{name}.{field["field"]}: '
                                         f'{why or f"{size} bytes do not hold {typed} ({natural} bytes)"}')
            else:
                report['skipped'].append(f'{name}.{field["field"]}: size 0')
                continue
            at = offset + size
        total = cls.get('size')
        if total is not None and total > at:
            members.append(f'unsigned __int8 pad_{at:x}[{total - at:#x}];')
            at = total
        if not members and ':' not in head:
            members.append('unsigned __int8 pad_0[1];')  # C has no empty struct.
        sizes[name] = at
        bodies.append(f'{head} {{ ' + ' '.join(members) + ' };')
        report['types'].append(name)
    lines += list(arrays.values()) + bodies
    report['arrays'] = list(arrays)
    lines.append('#pragma pack(pop)')
    return '\n'.join(lines) + '\n', report


def this_class(name, known):
    """The class a method label names: `Class::Method`, or `Class_Method` with the longest prefix
    `known(prefix)` accepts (class names hold '_' too). None when no prefix is known."""
    if '::' in name:
        head = name.rsplit('::', 1)[0]
        return head if known(head) else None
    parts = name.split('_')
    for cut in range(len(parts) - 1, 0, -1):
        head = '_'.join(parts[:cut])
        if known(head):
            return head
    return None


_EXEC = re.compile(r'^(?P<cls>.+)::exec(?P<method>\w+)$')


def exec_method(label):
    """(Class, Method) for an exec thunk's label `Class::execMethod`, else None."""
    match = _EXEC.match(label or '')
    return (match['cls'], match['method']) if match else None


def thunk_body(calls):
    """The native function an exec thunk calls, from its direct calls [(callee, passes_context)]:
    the one callee whose first argument is the thunk's own first parameter (P_THIS: the context
    object). The thunk's helpers take the stack frame first. None when no callee or several are."""
    found = {callee for callee, context in calls if context}
    return found.pop() if len(found) == 1 else None


def _context_calls(ea):
    """IDA: the direct calls in the decompiled function at `ea`, [(callee, passes_context)], or None
    when it does not decompile."""
    import ida_hexrays

    try:
        cfunc = ida_hexrays.decompile(ea)
    except Exception:  # noqa: BLE001 - a thunk that does not decompile keeps its body unnamed.
        return None
    if not cfunc or not cfunc.argidx.size():
        return None
    context = cfunc.argidx[0]
    calls = []

    class Calls(ida_hexrays.ctree_visitor_t):
        def visit_expr(self, expr):
            if expr.op == ida_hexrays.cot_call and expr.x.op == ida_hexrays.cot_obj:
                first = expr.a[0] if expr.a.size() else None
                while first is not None and first.op == ida_hexrays.cot_cast:
                    first = first.x
                calls.append((expr.x.obj_ea, first is not None and first.op == ida_hexrays.cot_var
                              and first.v.idx == context))
            return 0

    Calls(ida_hexrays.CV_FAST).apply_to(cfunc.body, None)
    return calls


def _thunk_bodies(named):
    """IDA: for each exec thunk [(start, `Class::execMethod`)], the native function it calls
    (thunk_body), named `Class::Method` (an auto name, as a save's) when its name is made up.
    Returns ([(start, name)] to type `this` on, the names given, and the
    thunks that did not decompile: they wait (while a load ends the decompiler refuses))."""
    import ida_funcs
    import ida_name

    out, given, waiting = [], [], []
    for ea, label in named:
        parts = exec_method(label)
        if not parts:
            continue
        calls = _context_calls(ea)
        if calls is None:
            waiting.append((ea, label))
            continue
        body = thunk_body(calls)
        if body is None or not ida_funcs.get_func(body):
            continue
        name = ida_name.get_name(body) or ''
        if made_up(name):
            name = f'{parts[0]}::{parts[1]}'
            if not ida_name.set_name(body, name, _auto_name()):
                continue  # Taken at another address.
            given.append(name)
        out.append((body, name))
    return out, given, waiting


def _number(value):
    """'0x..', decimal text or an int, as layout files write numbers."""
    return value if isinstance(value, int) else int(value, 0)


def layouts(lines):
    """(classes, enums, report) as declarations takes them, from layout-export's JSON lines (a line
    per class or struct, one per field it declares, one per enum; a line without `kind` is a field),
    or a string saying why the file cannot be used. A field's owner with no type line gets one of
    unknown size and super. An enum whose line gives no size takes it from a field that uses it.
    report: {'lines_skipped': ['line N: why']}."""
    classes, enums, skipped = {}, {}, []
    for number, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
            kind = row.get('kind', 'field')
            if kind in ('class', 'struct'):
                held = classes.setdefault(row['name'], {'fields': []})
                held.update(base=row.get('super'), size=_number(row['size']))
            elif kind == 'field':
                field = dict(row, offset=_number(row['offset']), size=_number(row.get('size', 0)))
                classes.setdefault(row['owner'], {'base': None, 'size': None, 'fields': []})['fields'].append(field)
            elif kind == 'enum':
                enums[row['name']] = {'size': row.get('size'), 'values': [(n, int(v)) for n, v in row['values']]}
            else:
                skipped.append(f'line {number}: unknown kind "{kind}"')
        except (ValueError, KeyError, TypeError, AttributeError) as error:
            skipped.append(f'line {number}: {error!r}')
    if not classes and not enums:
        return 'no layout lines: not a layout-export file'
    for cls in classes.values():
        for field in cls['fields']:
            for prop in (field, field.get('element'), field.get('key'), field.get('value')):
                if isinstance(prop, dict) and prop.get('enum') in enums and not enums[prop['enum']]['size']:
                    enums[prop['enum']]['size'] = prop.get('size') if prop.get('size') in _INTS else None
    return classes, enums, {'lines_skipped': skipped}


def import_layouts(path):
    """IDA: local types from a layout file (`layout-export`, with `closure` for the types fields name):
    a packed struct per class or struct, an enum per enum (see declarations). Types of the
    same name are replaced. Returns the declarations' report plus 'failed': declarations IDA refused,
    each with its first 80 characters (those types are missing, the rest are in)."""
    with open(path, encoding='utf-8') as handle:
        parsed = layouts(handle)
    if isinstance(parsed, str):
        raise ValueError(f'{path}: {parsed}')
    return _declare(*parsed)


def _declare(classes, enums, report):
    """IDA: declare what layouts() read; import_layouts' report."""
    import ida_typeinf

    text, built = declarations(classes, enums)
    flags = ida_typeinf.HTI_DCL | ida_typeinf.HTI_PAKDEF
    failed = []
    if ida_typeinf.parse_decls(None, text, None, flags):
        # Find which: each declaration alone, in order (forward declarations are in by now).
        for line in text.splitlines()[1:-1]:
            if ida_typeinf.parse_decls(None, f'#pragma pack(push, 1)\n{line}\n#pragma pack(pop)\n', None, flags):
                failed.append(line[:80])
    for key, value in built.items():
        report[key] = report.get(key, []) + value
    report['failed'] = failed
    return report


def _named_struct(name):
    """IDA: the local struct type named `name`, or None."""
    import ida_typeinf

    typed = ida_typeinf.tinfo_t()
    if typed.get_named_type(None, name) and typed.is_struct():
        return typed
    return None


def apply_this(labels_path):
    """IDA: for each label row of this build naming a function `Class::Method` or `Class_Method` whose
    class is a local struct type (import_layouts makes them), type the function's first argument
    `Class *this`. Other arguments and the return type stay as IDA has them (decompiler's guess when
    the function had no type). Returns {'typed': n, 'skipped': ['name: why']}."""
    with open(labels_path, encoding='utf-8') as handle:
        rows = load(handle)
    build = _database_build()
    if not build:
        raise ValueError('this database has no PE header and is no save')
    rows = of_build(rows, build['module_size'], build['timestamp'])
    if isinstance(rows, str):
        raise ValueError(rows)
    typed = _type_this([(build['base'] + row['rva'], row['name']) for row in best(rows)])
    return {'typed': typed['typed'], 'skipped': typed['skipped'] + [f'{name}: no decompiler' for _, name in typed['left']]}


def _type_this(named):
    """IDA: `Class *this` on each function [(start, name)] whose name gives a local struct type, and
    on the native function an exec thunk among them calls (_thunk_bodies names it `Class::Method`).
    Returns {'typed': n, 'skipped': ['name: why'], 'left': [(start, name)] with no function type
    yet (no decompiler), 'bodies': names given to thunks' native functions}."""
    import ida_auto
    import ida_funcs
    import ida_hexrays
    import ida_nalt
    import ida_typeinf

    ida_auto.auto_wait()
    structs, typed, skipped, left = {}, 0, [], []
    decompiler = ida_hexrays.init_hexrays_plugin()
    if decompiler:
        bodies, given, waiting = _thunk_bodies(named)
    else:  # Without the decompiler an exec thunk's native function is not found: the thunk waits.
        bodies, given, waiting = [], [], [(ea, label) for ea, label in named if exec_method(label)]
    # A native function the save listed too (as fn_<rva>) is typed under its new name, once.
    for ea, label in {**dict(named), **dict(bodies)}.items():
        function = ida_funcs.get_func(ea)
        if not function or function.start_ea != ea:
            continue  # A global, or a function this database does not hold.
        name = this_class(label, lambda head: structs[head] if head in structs else structs.setdefault(head, _named_struct(c_name(head))))
        if name is None:
            continue  # No class part names a type: not a method of an imported class.
        current = ida_typeinf.tinfo_t()
        # A type IDA holds, else its guess when that has arguments (a guess right after
        # analysis can be an empty one), else the decompiler's.
        if not ida_nalt.get_tinfo(current, ea) and ida_typeinf.guess_tinfo(current, ea) != ida_typeinf.GUESS_FUNC_OK:
            if not decompiler:
                left.append((ea, label))
                continue
            try:
                current = ida_hexrays.decompile(ea).type
            except Exception as error:  # noqa: BLE001
                skipped.append(f"{label}: no function type ({error})")
                continue
        details = ida_typeinf.func_type_data_t()
        if not current.get_func_details(details) or details.size() == 0:
            skipped.append(f"{label}: no first argument")
            continue
        pointer = ida_typeinf.tinfo_t()
        pointer.create_ptr(structs[name])
        details[0].type, details[0].name = pointer, 'this'
        changed = ida_typeinf.tinfo_t()
        if changed.create_func(details) and ida_typeinf.apply_tinfo(ea, changed, ida_typeinf.TINFO_DEFINITE):
            typed += 1
        else:
            skipped.append(f"{label}: IDA refused the type")
    left += [item for item in waiting if item not in left]
    return {'typed': typed, 'skipped': skipped, 'left': left, 'bodies': given}
