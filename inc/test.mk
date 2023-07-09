
.PHONY: test
test: obj obj/json-test
	obj/json-test -q

test-clean:
	rm -r obj 2>/dev/null; true

obj:
	mkdir obj

obj/json-test: obj/json-test.o snac.o
	$(CC) $(CFLAGS) -L/usr/local/lib $< snac.o $(libs) -o $@

json_test_deps != $(CC) $(CFLAGS) -I/usr/local/include -MM test/json-test.c | sed -r 'sX.*:XX' | tr -d '\\\n'
obj/json-test.o: test/json-test.c $(json_test_deps)
	$(CC) $(CFLAGS) -I/usr/local/include -c $< -o $@


