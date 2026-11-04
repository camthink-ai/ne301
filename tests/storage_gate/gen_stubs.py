import os
import re
import subprocess
import sys

objs = sys.argv[1:]
known = set()
libc_names = (
    "printf snprintf sprintf fprintf vsnprintf puts putchar abort exit "
    "memcpy memset memmove memcmp memchr strlen strcpy strncpy strcat strncat "
    "strcmp strncmp strcasecmp strchr strrchr strstr strtok strtol strtoul strtoull strtof strtod "
    "calloc malloc realloc free sscanf strcspn strspn strdup strerror isdigit isalpha isspace isupper tolower toupper "
    "fopen fclose fread fwrite fseek ftell fflush remove rename mkdir stat "
    "time gmtime localtime strftime atoi bzero setvbuf qsort bsearch abs labs exit atol"
).split()
libc = re.compile(r"^_(" + "|".join(libc_names) + r")($|_)")
builtin = re.compile(r"^__|^_\w+_chk$")

undef_flag = "-u"

for o in objs:
    out = subprocess.run(["nm", "-g", o], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-2] != "U":
            known.add(parts[-1])

undef = set()
for o in objs:
    out = subprocess.run(["nm", undef_flag, o], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] == "U":
            undef.add(parts[-1])
        elif len(parts) == 1 and parts[0]:
            undef.add(parts[0])

libc_syms = set()
libc_probe = subprocess.run(["cc", "-print-file-name=libc.so.6"], capture_output=True, text=True).stdout.strip()
if libc_probe.startswith("/") and os.path.exists(libc_probe):
    out = subprocess.run(["nm", "-D", "--defined-only", libc_probe], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            libc_syms.add(parts[-1].split("@")[0])

missing = set()
for sym in undef:
    if sym in known:
        continue
    if sym in libc_syms or sym.lstrip("_") in libc_syms:
        continue
    name = sym[1:] if sym.startswith("_") else sym
    if libc.match("_" + name):
        continue
    if builtin.match(sym) or builtin.match(name):
        continue
    missing.add(name)

with open("gen_stubs.c", "w") as f:
    f.write("#include <stdint.h>\n")
    f.write("#include <stddef.h>\n\n")
    for name in sorted(missing):
        f.write("void %s(void) {}\n" % name)

print("generated %d stubs" % len(missing))
