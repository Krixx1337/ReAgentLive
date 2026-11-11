// reagentlive CLI: console syntax for the core and Unreal pack ops.
#pragma once
#include <span>
#include <string_view>
#include "reagentlive/frontends/cli/syntax/command.h"
#include "reagentlive/frontends/cli/syntax/text.h"

// Console syntax → Request. Host side only; pure, so tests cover it.
//   find <text>                       object names containing text
//   read <addr> [len] [--as <type>]   len defaults to 64; --as decodes elements
//   write <addr> <hex bytes>          "01 02 ff" or "0102ff"
//   call <object> <[Class:]Function> [hex params]
//   base                              exe base and size (cached at attach)
//   modules                           loaded modules, exe first
//   search <type> <value> [options]   addresses holding a value (see SearchUsage)
//   pin <addr> <hex> [options] | pin list | pin clear [id]
//   watch <seconds|Nms> <addr>:<len>... [--then call ...]
//   fields <object>                   reflected properties (class/struct: its own)
//   dump <object> [filters]           fields with their current values (instances)
//   get <object> <Field.Path>...      named fields, following object pointers
//   batch { <command>; ... }          commands held in as few host ticks as possible
//   name <index> [index ...]          FName comparison indices as text
//   sig --check <pattern> [options]   is a byte signature unique, and where it lands
//   sig --check --file <patterns>     several at once (JSON lines)
//   sig --bytes <addr> [len]          live bytes in both pattern forms, addresses masked
//   disasm <addr> [count] [--function] [--follow]   instructions decoded from live bytes
//   xrefs <addr> [--module M] [--all] what points here: relative fields and pointers
//   touches <addr> <offset>... [--range N]   functions near addr using [reg + offset]
//   vtable <addr> [count] [--offset N] [--table] [--resolve]   an object's virtual table
//   region <addr>                     what the memory there is: code, rodata, data, private, ...
// Numbers: 0x-prefixed hex, otherwise decimal. <addr> and <object> accept $N
// (result N of the last find) or a literal ref 0x<address>#<index>:<serial>
// as the console prints it, so refs survive between console runs. Addresses
// also accept +offset ($2+0x120), "base" for the exe (base+0x3ef8ee0) and a
// loaded module's name (game.exe+0x3ef8ee0) once the console has the module
// list; a module offset must lie inside the image.
namespace reagentlive::cli {
// modules: the loaded modules, exe first, or empty while unknown ("base" and
// module names then fail).
Parsed Parse(std::string_view line, std::span<const Ref> refs, std::span<const Module> modules = {});
}
