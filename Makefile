CC=/home/rumble/intel/cce/10.0.025/bin/icc
CFLAGS=-mcpu=pentium4 -O3

rmapper: rmapper.c fasta.o sw-vector.o sw-full.o util.o
	$(CC) $(CFLAGS) -o $@ $+

fasta.o: fasta.c fasta.h
	$(CC) $(CFLAGS) -c -o $@ $<

sw-vector.o: sw-vector.c rmapper.h
	$(CC) $(CFLAGS) -c -o $@ $<

sw-full.o: sw-full.c rmapper.h
	$(CC) $(CFLAGS) -c -o $@ $<

util.o: util.c util.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.core rmapper
