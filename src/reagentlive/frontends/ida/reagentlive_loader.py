"""ReAgentLive's IDA loader stub: this file goes in IDA's user loaders folder; the code is the
reagentlive_ida package beside it in the repo (tools/ida_setup.py --install does both).

The functions are defined here rather than imported: IDAPython runs a loader function with
this module's namespace as its globals. They call through the module, so reagentlive_ida.reload()
reaches them too.
"""
try:
    from reagentlive_ida import loader as _loader
except ImportError as _error:
    _loader = None
    print(f'ReAgentLive loader: {_error}; run tools/ida_setup.py --install with IDA\'s Python')


def accept_file(li, filename):
    return _loader.accept_file(li, filename) if _loader else 0


def load_file(li, neflags, format):
    return _loader.load_file(li, neflags, format) if _loader else 0
