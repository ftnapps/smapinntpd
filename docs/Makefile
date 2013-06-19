# doc/Makefile

all: manpages htmldocs

manpages: makechs.1 smapinntpd.1

htmldocs: makechs.html smapinntpd.html

makechs.1: makechs.txt
	a2x --doctype manpage --format manpage makechs.txt

smapinntpd.1: smapinntpd.txt
	a2x --doctype manpage --format manpage smapinntpd.txt

makechs.html: makechs.txt
	asciidoc -b html makechs.txt

smapinntpd.html: smapinntpd.txt
	asciidoc -b html smapinntpd.txt

clean:
	rm -f makechs.html
	rm -f smapinntpd.html
	rm -f makechs.1
	rm -f smapinntpd.1
