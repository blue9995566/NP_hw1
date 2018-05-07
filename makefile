OBSJ=server.o
LIBS=-lm
main:$(OBSJ)
	gcc -o server $(OBSJ) $(LIBS)
clean:
	rm -f $(OBSJ) server