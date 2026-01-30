
# Copyright 2025 Aeva Palecek
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from .backend import profiling_enabled, profiling_scope


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
