# init: starts the services and programs of etc/init.cfg, restarts them when they end
crtos_app(init SOURCES init.c STACK 8192 HEAP 32768)
