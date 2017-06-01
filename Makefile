.SILENT:

subfiles = lab3a.c ext2_fs.h Makefile README

default:
	gcc -o lab3a lab3a.c -g
dist:
	tar -zcvf lab3a-604-489-201.tar.gz $(subfiles)
clean:
	rm lab3a *.tar.gz