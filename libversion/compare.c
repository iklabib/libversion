/*
 * Copyright (c) 2017-2019 Dmitry Marakasov <amdmi3@amdmi3.ru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <libversion/version.h>

#include <stdint.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MY_MIN(a, b) ((a) < (b) ? (a) : (b))

#if defined(INT64_MAX)
	typedef int64_t version_component_t;
	#define VERSION_COMPONENT_MAX INT64_MAX
#elif defined(LLONG_MAX)
	typedef long long version_component_t;
	#define VERSION_COMPONENT_MAX LLONG_MAX
#else
	typedef long version_component_t;
	#define VERSION_COMPONENT_MAX LONG_MAX
#endif

typedef struct {
	version_component_t a;
	version_component_t b;
	version_component_t c;
} unit_t;

static int compare_units(const unit_t* u1, const unit_t* u2) {
	if (u1->a < u2->a)
		return -1;
	if (u1->a > u2->a)
		return 1;
	if (u1->b < u2->b)
		return -1;
	if (u1->b > u2->b)
		return 1;
	if (u1->c < u2->c)
		return -1;
	if (u1->c > u2->c)
		return 1;
	return 0;
}

static int is_version_char(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static version_component_t parse_number(const char** str) {
	const char* cur = *str;
	version_component_t component = 0;
	while (*cur >= '0' && *cur <= '9') {
		char number = *cur - '0';

		if (component <= (VERSION_COMPONENT_MAX - number) / 10) {
			component = component * 10 + number;
		} else {
			component = VERSION_COMPONENT_MAX;
		}

		cur++;
	}

	if (cur == *str)
		return -1;

	*str = cur;
	return component;
}

enum {
	ALPHAFLAG_PRERELEASE = 1,
	ALPHAFLAG_POSTRELEASE = 2
};

static int mymemcasecmp(const char* a, const char* b, size_t len) {
	while (len != 0) {
		unsigned char ua = (unsigned char)((*a >= 'A' && *a <= 'Z') ? (*a - 'A' + 'a') : (*a));
		unsigned char ub = (unsigned char)((*b >= 'A' && *b <= 'Z') ? (*b - 'A' + 'a') : (*b));

		if (ua != ub)
			return ua - ub;

		a++;
		b++;
		len--;
	}

	return 0;
}

static version_component_t parse_alpha(const char** str, int* outflags, int flags) {
	char start = **str;

	const char* cur = *str;

	while ((*cur >= 'a' && *cur <= 'z') || (*cur >= 'A' && *cur <= 'Z'))
		cur++;

	*outflags = 0;

	if (cur == *str)
		return -1;
	else if (cur - *str == 5 && mymemcasecmp(*str, "alpha", 5) == 0)
		*outflags = ALPHAFLAG_PRERELEASE;
	else if (cur - *str == 4 && mymemcasecmp(*str, "beta", 4) == 0)
		*outflags = ALPHAFLAG_PRERELEASE;
	else if (cur - *str == 2 && mymemcasecmp(*str, "rc", 2) == 0)
		*outflags = ALPHAFLAG_PRERELEASE;
	else if (cur - *str >= 3 && mymemcasecmp(*str, "pre", 3) == 0)
		*outflags = ALPHAFLAG_PRERELEASE;
	else if (cur - *str >= 4 && mymemcasecmp(*str, "post", 4) == 0)
		*outflags = ALPHAFLAG_POSTRELEASE;
	else if (cur - *str == 5 && mymemcasecmp(*str, "patch", 5) == 0)
		*outflags = ALPHAFLAG_POSTRELEASE;
	else if (cur - *str == 2 && mymemcasecmp(*str, "pl", 2) == 0)  /* patchlevel */
		*outflags = ALPHAFLAG_POSTRELEASE;
	else if (cur - *str == 6 && mymemcasecmp(*str, "errata", 2) == 0)
		*outflags = ALPHAFLAG_POSTRELEASE;
	else if (flags & VERSIONFLAG_P_IS_PATCH && cur - *str == 1 && (**str == 'p' || **str == 'P'))
		*outflags = ALPHAFLAG_POSTRELEASE;

	*str = cur;

	if (start >= 'A' && start <= 'Z')
		return start - 'A' + 'a';  /* lowercase */
	else
		return start;
}

static size_t get_next_version_component(const char** str, unit_t* target, int flags) {
	const char* end;
	version_component_t number, alpha, extranumber;
	int alphaflags = 0;

	/* skip separators */
	while (**str != '\0' && !is_version_char(**str))
		++*str;

	/* EOL, generate filler component */
	if (**str == '\0') {
		if (flags & VERSIONFLAG_LOWER_BOUND) {
			target->a = -2;
			target->b = -2;
			target->c = -2;
		} else if (flags & VERSIONFLAG_UPPER_BOUND) {
			target->a = VERSION_COMPONENT_MAX;
			target->b = VERSION_COMPONENT_MAX;
			target->c = VERSION_COMPONENT_MAX;
		} else {
			target->a = 0;
			target->b = -1;
			target->c = -1;
		}
		return 1;
	}

	end = *str;
	while (is_version_char(*end))
		end++;

	/* parse component from string [str; end) */
	number = parse_number(str);
	alpha = parse_alpha(str, &alphaflags, flags);
	extranumber = parse_number(str);

	/* skip remaining alphanumeric part */
	while (is_version_char(**str))
		++*str;

	if (flags & VERSIONFLAG_ANY_IS_PATCH)
		alphaflags = ALPHAFLAG_POSTRELEASE;

	if (number != -1 && extranumber != -1) {
		/*
		 * `1a1' -> treat as [1  ].[ a1]
		 * `1patch1' -> special case, treat as [1  ].[0p1]
		 */
		target->a = number;
		target->b = -1;
		target->c = -1;
		target++;
		target->a = (alphaflags == ALPHAFLAG_POSTRELEASE) ? 0 : -1;
		target->b = alpha;
		target->c = extranumber;
		return 2;
	} else if (number != -1 && alpha != -1 && alphaflags) {
		/*
		 * when alpha part is known to mean prerelease,
		 * not a version addendum, unglue it from number
		 *
		 * `1alpha' is treated as [1  ].[ a ], not [1a ]
		 */
		target->a = number;
		target->b = -1;
		target->c = -1;
		target++;
		target->a = (alphaflags == ALPHAFLAG_POSTRELEASE) ? 0 : -1;
		target->b = alpha;
		target->c = -1;
		return 2;
	} else {
		if (number == -1 && alphaflags == ALPHAFLAG_POSTRELEASE)
			number = 0;
		target->a = number;
		target->b = alpha;
		target->c = extranumber;
		return 1;
	}
}

int version_compare4(const char* v1, const char* v2, int v1_flags, int v2_flags) {
	unit_t v1_units[2], v2_units[2];
	size_t v1_len = 0, v2_len = 0;
	size_t shift, i;

	int v1_extra_components = (v1_flags & (VERSIONFLAG_LOWER_BOUND|VERSIONFLAG_UPPER_BOUND)) ? 1 : 0;
	int v2_extra_components = (v2_flags & (VERSIONFLAG_LOWER_BOUND|VERSIONFLAG_UPPER_BOUND)) ? 1 : 0;

	int v1_exhausted, v2_exhausted;

	int res;

	do {
		if (v1_len == 0)
			v1_len = get_next_version_component(&v1, v1_units, v1_flags);
		if (v2_len == 0)
			v2_len = get_next_version_component(&v2, v2_units, v2_flags);

		shift = MY_MIN(v1_len, v2_len);
		for (i = 0; i < shift; i++) {
			res = compare_units(&v1_units[i], &v2_units[i]);
			if (res != 0)
				return res;
		}

		if (v1_len != v2_len) {
			for (i = 0; i < shift; i++) {
				v1_units[i] = v1_units[i+shift];
				v2_units[i] = v2_units[i+shift];
			}
		}

		v1_len -= shift;
		v2_len -= shift;

		v1_exhausted = *v1 == '\0' && v1_len == 0;
		v2_exhausted = *v2 == '\0' && v2_len == 0;

		if (v1_exhausted && v1_extra_components > 0) {
			v1_extra_components--;
			v1_exhausted = 0;
		}
		if (v2_exhausted && v2_extra_components > 0) {
			v2_extra_components--;
			v2_exhausted = 0;
		}
	} while (!v1_exhausted || !v2_exhausted);

	return 0;
}

int version_compare2(const char* v1, const char* v2) {
	return version_compare4(v1, v2, 0, 0);
}

/* deprecated */
int version_compare3(const char* v1, const char* v2, int flags) {
	return version_compare4(v1, v2, flags, flags);
}

int version_compare_simple(const char* v1, const char* v2) {
	return version_compare2(v1, v2);
}

int version_compare_flags(const char* v1, const char* v2, int flags) {
	const int v1_flags =
		((flags & VERSIONFLAG_P_IS_PATCH_LEFT) ? VERSIONFLAG_P_IS_PATCH : 0) |
		((flags & VERSIONFLAG_ANY_IS_PATCH_LEFT) ? VERSIONFLAG_ANY_IS_PATCH : 0);
	const int v2_flags =
		((flags & VERSIONFLAG_P_IS_PATCH_RIGHT) ? VERSIONFLAG_P_IS_PATCH : 0) |
		((flags & VERSIONFLAG_ANY_IS_PATCH_RIGHT) ? VERSIONFLAG_ANY_IS_PATCH : 0);

	return version_compare4(v1, v2, v1_flags, v2_flags);
}

int version_compare_flags2(const char* v1, const char* v2, int v1_flags, int v2_flags) {
	return version_compare4(v1, v2, v1_flags, v2_flags);
}
