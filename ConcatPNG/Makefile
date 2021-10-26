all: paster

paster: bin/paster.c png_utils/parse_chunk.c png_utils/zutil.c png_utils/crc.c
	gcc -g --std=c99 -o paster $^ -lcurl -lz  -D_GNU_SOURCE -pthread

.PHONY: clean
clean:
	rm -f paster all.png
