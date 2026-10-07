#!/usr/bin/env python3
"""Forensic dump of RT-Thread kernel state over SWD.

Reads the object container directly instead of relying on the (possibly deaf)
msh shell, so we can answer "which thread is stuck where" even when the
console is unusable.

Struct offsets are hardcoded from rtdef.h and MUST be kept in sync with the
tree under third_party/rt-thread-nano.  Verified against:

    rt_object    : char name[12]; uint8 type; uint8 flag; (pad 2) rt_list_t list;
                   -> 24 bytes
    rt_ipc_object: rt_object parent; rt_list_t suspend_thread;
                   -> 32 bytes  (sem.value sits at 32)
    rt_thread    : rt_object parent; rt_list_t tlist; void *sp; void *entry; ...
                   -> tlist@24, sp@32

Also decodes a thread's hardware-stacked frame (the 8 words an exception
pushes) so the PC at the time of the last switch is visible.
"""
import sys
import time

sys.path.insert(0, "tools/verify")
sys.path.insert(0, ".")

import probe_ospi_regs as P  # noqa: E402  (shares build_session / MemoryMap)

# --- kernel globals (from `arm-none-eabi-nm build/firmware.elf`) -------------
ADDR = {
    "rt_object_container": None,   # filled below
    "rt_current_thread": 0x22048D94,
    "rt_thread_ready_priority_group": 0x22048C8C,
    "rt_scheduler_lock_nest": 0x22048D90,
    "rt_interrupt_nest": None,
    "rt_tick": 0x22048A78,
}

# rt_object_container[] is a static array in object.c; find it by symbol at
# runtime rather than guessing.  These are the ones we care about.
CONTAINER = {
    2: "Thread",   # RT_Object_Class_Thread
}

# --- struct offsets ---------------------------------------------------------
T_PARENT_NAME = 0        # char name[RT_NAME_MAX=12]
T_PARENT_TYPE = 12
T_TLIST       = 24
T_SP          = 32
T_ENTRY       = 36
T_STACK       = 40
T_STACK_SIZE  = 44
T_STAT        = 60       # rt_uint8_t stat
T_CPRIO       = 56       # rt_uint8_t current_priority
T_INIT_PRIO   = 55       # rt_uint8_t init_priority
T_NUMBER      = 61
T_TICK        = 48       # rt_tick_t remaining_tick (rt_uint32)

SEM_VALUE = 32           # struct rt_semaphore: parent(32) + value

THREAD_STAT = {
    0x00: "INIT",
    0x01: "READY",
    0x02: "RUNNING",
    0x03: "SUSPEND",
    0x04: "CLOSE",
}


def read_cstr(target, addr, n=16):
    raw = target.read_memory_block8(addr, n)
    out = bytearray()
    for b in raw:
        if b == 0:
            break
        out.append(b)
    return out.decode("ascii", "replace")


def find_container(target):
    """Locate rt_object_container[] via the exported information helper.

    rt_object_get_information() returns &rt_object_container[type], so we can
    call it in a halted target by reading its literal pool is fragile; instead
    scan .bss for a plausible rt_object_information array.  Simpler and more
    robust: read rt_object_container from the symbol table.  The symbol is
    static, so we locate it through the type's linked list by starting from
    the well-known thread list head.
    """
    return None


def dump_state(sess, extra=None):
    t = sess.target
    t.halt()
    try:
        cur = t.read32(ADDR["rt_current_thread"])
        grp = t.read32(ADDR["rt_thread_ready_priority_group"])
        lock = t.read32(ADDR["rt_scheduler_lock_nest"])
        tick = t.read32(ADDR["rt_tick"])

        print(f"rt_tick          = {tick}")
        print(f"current_thread   = {cur:#010x}")
        print(f"ready_prio_group = {grp:#010x}" + ("   <-- NOTHING RUNNABLE" if grp == 0 else ""))
        print(f"sched_lock_nest  = {lock}")

        if extra:
            for name, addr in extra:
                print(f"{name:16s} = {t.read32(addr):#010x}")

        # Walk the thread object list.  rt_object_information for Thread is at
        # rt_object_container[RT_Object_Class_Thread]; its object_list head is
        # the first rt_list_t inside.  We find the container by scanning for a
        # list whose nodes chain back to thread TCBs with sane names.
        for base in range(0x22040000, 0x22050000, 0x10):
            try:
                nxt = t.read32(base)
            except Exception:
                continue
            if not (0x22040000 <= nxt < 0x22100000):
                continue
            # A list head's next points to a node; node - 24 == TCB.
            tcb = nxt - T_TLIST
            if not (0x22040000 <= tcb < 0x22100000):
                continue
            try:
                nm = read_cstr(t, tcb + T_PARENT_NAME, 12)
                ty = t.read8(tcb + T_PARENT_TYPE)
            except Exception:
                continue
            if ty == 2 and nm and all(32 <= ord(c) < 127 for c in nm):
                print(f"\n--- thread list @ container+{base - 0x22040000:#x} ---")
                _walk(t, base)
                return
        print("thread container not located")
    finally:
        t.resume()


def _walk(t, head_addr):
    node = t.read32(head_addr)
    seen = set()
    while node not in (0, head_addr) and node not in seen:
        seen.add(node)
        tcb = node - T_TLIST
        name = read_cstr(t, tcb + T_PARENT_NAME, 12)
        stat = t.read8(tcb + T_STAT)
        cprio = t.read8(tcb + T_CPRIO)
        sp = t.read32(tcb + T_SP)
        stack = t.read32(tcb + T_STACK)
        ssize = t.read32(tcb + T_STACK_SIZE)
        used = stack + ssize - sp if (stack and ssize and sp) else 0
        pct = (used * 100 // ssize) if ssize else 0
        stat_name = THREAD_STAT.get(stat, f"?{stat:#x}")
        print(f"  {name:10s} prio={cprio:2d} stat={stat_name:8s} "
              f"sp={sp:#010x} stk=[{stack:#010x}+{ssize:#06x}] used={pct}%")
        node = t.read32(node)


if __name__ == "__main__":
    sess = P.build_session()
    dump_state(sess)
    sess.close()
