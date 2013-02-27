export TIMEFORMAT=%Rs

benchmark:      \
    world95.txt \
    bible.txt   \
    enwik7.txt
	@ make --no-print-directory SAMPLE=world95 sample_benchmark
	@ make --no-print-directory SAMPLE=bible   sample_benchmark
	@ make --no-print-directory SAMPLE=enwik7  sample_benchmark
.PHONY: all

clean:
	@ make --no-print-directory SAMPLE=world95 sample_clean
	@ make --no-print-directory SAMPLE=bible   sample_clean
	@ make --no-print-directory SAMPLE=enwik7  sample_clean
.PHONY: clean

sample_benchmark: $(SAMPLE).txt \
    ../bin/comprox              \
    ../bin/comprolz             \
    ../bin/comprop
	@ echo
	@ echo    -e "Benchmark with *$(SAMPLE).txt* (original size = $(shell wc -c < $(SAMPLE).txt)):"
	@
	@ echo -n -e "            encode"
	@ echo -n -e "            decode"
	@ echo    -e "            compressed size"
	@
	@ echo -n -e "gzip:     "; printf "%8s" $$({ time gzip  < $(SAMPLE).txt >$(SAMPLE).gz          ;} 2>&1)
	@ echo -n -e "          "; printf "%8s" $$({ time gzip  -d < $(SAMPLE).gz | cmp $(SAMPLE).txt  ;} 2>&1)
	@ echo -n -e "          "; printf "%9s" $$({ wc -c < $(SAMPLE).gz ;} 2>&1)
	@ echo
	@ echo -n -e "bzip2:    "; printf "%8s" $$({ time bzip2 < $(SAMPLE).txt >$(SAMPLE).bz2         ;} 2>&1)
	@ echo -n -e "          "; printf "%8s" $$({ time bzip2 -d < $(SAMPLE).bz2 | cmp $(SAMPLE).txt ;} 2>&1)
	@ echo -n -e "          "; printf "%9s" $$({ wc -c < $(SAMPLE).bz2 ;} 2>&1)
	@ echo
	@ echo -n -e "xz:       "; printf "%8s" $$({ time xz    < $(SAMPLE).txt >$(SAMPLE).xz          ;} 2>&1)
	@ echo -n -e "          "; printf "%8s" $$({ time xz    -d < $(SAMPLE).xz  | cmp $(SAMPLE).txt ;} 2>&1)
	@ echo -n -e "          "; printf "%9s" $$({ wc -c < $(SAMPLE).xz ;} 2>&1)
	@ echo
	@ echo -n -e "comprox:  "; printf "%8s" $$({ time ../bin/comprox  -q e $(SAMPLE).txt >$(SAMPLE).crox       ;} 2>&1)
	@ echo -n -e "          "; printf "%8s" $$({ time ../bin/comprox  -q d $(SAMPLE).crox  | cmp $(SAMPLE).txt ;} 2>&1)
	@ echo -n -e "          "; printf "%9s" $$({ wc -c < $(SAMPLE).crox ;} 2>&1)
	@ echo
	@ echo -n -e "comprolz: "; printf "%8s" $$({ time ../bin/comprolz -q e $(SAMPLE).txt >$(SAMPLE).crolz      ;} 2>&1)
	@ echo -n -e "          "; printf "%8s" $$({ time ../bin/comprolz -q d $(SAMPLE).crolz | cmp $(SAMPLE).txt ;} 2>&1)
	@ echo -n -e "          "; printf "%9s" $$({ wc -c < $(SAMPLE).crolz ;} 2>&1)
	@ echo
	@ echo -n -e "comprop:  "; printf "%8s" $$({ time ../bin/comprop  -q e $(SAMPLE).txt >$(SAMPLE).crop       ;} 2>&1)
	@ echo -n -e "          "; printf "%8s" $$({ time ../bin/comprop  -q d $(SAMPLE).crop  | cmp $(SAMPLE).txt ;} 2>&1)
	@ echo -n -e "          "; printf "%9s" $$({ wc -c < $(SAMPLE).crop ;} 2>&1)
	@ echo
.PHONY: benchmark

sample_clean:
	@ rm -f $(SAMPLE).gz
	@ rm -f $(SAMPLE).bz2
	@ rm -f $(SAMPLE).xz
	@ rm -f $(SAMPLE).crox
	@ rm -f $(SAMPLE).crolz
	@ rm -f $(SAMPLE).crop
.PHONY: sample_clean

../bin/%:
	@ make -C ../ ./bin/$(notdir $@)

%.txt:
	@ wget  http://comprox.googlecode.com/svn/$@
	@ touch $@
