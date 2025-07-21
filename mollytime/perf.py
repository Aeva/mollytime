
from mollytime import profiling_enabled, profiling_scope


def profile_function(name):
    def decorator(function):
        if profiling_enabled():
            def wrapper(*args, **kargs):
                def thunk():
                    return function(*args, **kargs)
                return profiling_scope(name, thunk)
            return wrapper
        else:
            return function
    return decorator
