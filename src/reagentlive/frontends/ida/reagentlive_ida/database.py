"""What a database keeps for ReAgentLive (netnodes), and the flags of a name from a file. IDA only."""
import json

_NODE = '$ reagentlive build'  # The database's build, so add_saved refuses another one.
_CONSOLE_NODE = '$ reagentlive console'  # The latest save's console row: fetch, workspace and finish default to it.


def _auto_name():
    """IDA: set_name flags for a name from a file, not the user's (export_names skips it)."""
    import ida_name
    return ida_name.SN_AUTO | ida_name.SN_NOCHECK | ida_name.SN_NOWARN


def recorded_console():
    """IDA: the console row the database's saves recorded, {path, labels, names}, or None (a full
    database: no save recorded one)."""
    import ida_netnode

    node = ida_netnode.netnode(_CONSOLE_NODE, 0, False)
    if node.index() == ida_netnode.BADNODE or not node.supstr(0):
        return None
    return json.loads(node.supstr(0))
