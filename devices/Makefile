.PHONY: all h4h ramdrive ramdrive_timing libdummy libramdrive #dragonfire  
all: all h4h ramdrive ramdrive_timing libdummy libramdrive #dragonfire

h4h:
	cd h4h; $(MAKE) clean; $(MAKE)

ramdrive:
	cd ramdrive; $(MAKE) clean; $(MAKE)

ramdrive_timing:
	cd ramdrive_timing; $(MAKE) clean; $(MAKE)

libdummy:
	cd libdummy; $(MAKE) clean; $(MAKE)

libramdrive:
	cd libramdrive; $(MAKE) clean; $(MAKE)

dragonfire:
	cd dragonfire; $(MAKE) clean; $(MAKE)

.PHONY: clean 
clean: 
	@cd h4h; $(MAKE) clean
	@cd ramdrive; $(MAKE) clean
	@cd ramdrive_timing; $(MAKE) clean
	@cd libdummy; $(MAKE) clean
	@cd libramdrive; $(MAKE) clean
	@cd ramdrive_dragonfire; $(MAKE) clean
