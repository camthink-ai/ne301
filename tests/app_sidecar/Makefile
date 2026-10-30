.PHONY: test clean build/vectors

test:
	$(MAKE) -C ../app_unsigned test

# The asset/bridge suites request this fixture target directly.
build/vectors:
	python3 gen_sidecar_fixtures.py --output build

clean:
	$(MAKE) -C ../app_unsigned clean
