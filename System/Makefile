.PHONY: all score shal sos clean

all: score

score:
	$(MAKE) -C score

shal:
	$(MAKE) -C shal

sos:
	$(MAKE) -C sos

clean:
	$(MAKE) -C score clean
	@if [ -f shal/Makefile ]; then $(MAKE) -C shal clean; fi
	@if [ -f sos/Makefile ]; then $(MAKE) -C sos clean; fi
