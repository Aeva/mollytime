#subdir('generic')

if host_machine.system() == 'windows'
  subdir('windows')
elif host_machine.system() == 'emscripten'
  cdata.set('SDL_THREAD_PTHREAD', 1)
  subdir('pthread')
else
  subdir('pthread')
endif

sources += files('SDL_thread.c')
