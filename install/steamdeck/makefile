# Stage a Steam Deck-ready client folder in run\steamdeck.
#
# Run "nmake" (or "nmake RELEASE=1") at the top of the tree first: this target
# copies what the client build put in run\localclient, it does not build it.
#
# Not included in the top-level "all" target; invoke it with
#
#   nmake steamdeck
#
# from the top of the tree, or "nmake" from this directory.

TOPDIR=..\..
!include $(TOPDIR)\common.mak

SOURCEDIR = $(TOPDIR)\install\steamdeck
DECKDIR   = $(TOPDIR)\run\steamdeck

all: package

package:
	@echo Staging Steam Deck client in $(DECKDIR)
	-@if not exist $(DECKDIR) mkdir $(DECKDIR)
	xcopy /E /I /Y /Q /EXCLUDE:$(SOURCEDIR)\exclude.txt $(CLIENTRUNDIR) $(DECKDIR)
# The Deck config replaces the desktop one: 60Hz cap, gamepad bindings.
	$(CP) $(SOURCEDIR)\config.ini $(DECKDIR)
	$(CP) $(SOURCEDIR)\README.md $(DECKDIR)
	$(CP) $(SOURCEDIR)\install-steamdeck.sh $(DECKDIR)
	@echo.
	@echo Copy $(DECKDIR) to the Deck and run install-steamdeck.sh there.
	@echo See $(SOURCEDIR)\README.md for the Steam setup steps.

clean:
	-@if exist $(DECKDIR) rmdir /S /Q $(DECKDIR)
