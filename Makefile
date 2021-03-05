
smi: smi.o
	$(CC) $< -o $@

clean:
	rm -f smi.o smi
