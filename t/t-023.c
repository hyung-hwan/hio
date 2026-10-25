/* Socket-address parser and formatter buffer boundaries. */
#include "hio-prv.h"
#include <hio-skad.h>
#include <hio-nwif.h>
#include "skad-prv.h"
#include "tap.h"

#if defined(HAVE_UNISTD_H) && !defined(_WIN32) && !defined(__OS2__) && !defined(__DOS__)
#include <unistd.h>
#endif


static const hio_uch_t wide_loopback[] = { '1', '2', '7', '.', '0', '.', '0', '.', '1', 0 };
static const hio_uch_t wide_invalid[] = { 'i', 'n', 'v', 'a', 'l', 'i', 'd', 0 };
static const hio_uch_t wide_doc[] = { '1', '9', '2', '.', '0', '.', '2', '.', '1', 0 };
static const hio_ooch_t generic_loopback[] = { '1', '2', '7', '.', '0', '.', '0', '.', '1', 0 };
static const hio_ooch_t generic_invalid[] = { 'i', 'n', 'v', 'a', 'l', 'i', 'd', 0 };

static void test_native_alignment (void)
{
	struct
	{
		hio_uint8_t prefix;
		hio_skad_t address;
	} embedded;
	hio_skad_t addresses[2];
	hio_skad_t* ptrs[3];
	hio_oow_t alignment, i;

	alignment = HIO_ALIGNOF(hio_skad_alt_t);
	OK(HIO_SIZEOF(hio_skad_t) >= HIO_SIZEOF(hio_skad_alt_t),
		"public address storage accommodates the native address union");
	OK(HIO_ALIGNOF(hio_skad_t) >= alignment,
		"public address type provides native alignment");
	OK(HIO_SIZEOF(hio_skad_t) % alignment == 0,
		"public address array stride preserves native alignment");
	ptrs[0] = &addresses[0];
	ptrs[1] = &addresses[1];
	ptrs[2] = &embedded.address;
	for (i = 0; i < HIO_COUNTOF(ptrs); i++)
	{
		OK((hio_uintptr_t)ptrs[i] % alignment == 0,
			"array elements and an address following a byte field have native alignment");
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		hio_skad_init_for_ip4(ptrs[i], 80, HIO_NULL);
		OK(hio_skad_get_family(ptrs[i]) == HIO_AF_INET && hio_skad_get_port(ptrs[i]) == 80,
			"IPv4 initialization and access work in arrays and embedded structures");
#else
		skip("IPv4 addresses are unavailable", 1);
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		hio_skad_init_for_ip6(ptrs[i], 443, HIO_NULL, 7);
		OK(hio_skad_get_family(ptrs[i]) == HIO_AF_INET6 &&
		   hio_skad_get_port(ptrs[i]) == 443 && hio_skad_get_scope_id(ptrs[i]) == 7,
			"IPv6 initialization and access work in arrays and embedded structures");
#else
		skip("IPv6 addresses are unavailable", 1);
#endif
	}
}

static void test_invalid_lengths (void)
{
	hio_uint8_t raw[32] = { 0 };
	hio_bch_t bytes[36];
	hio_uch_t wide[36];
	hio_oow_t lengths[] = { 0, 1, 3, 5, 15, 17, 32 };
	hio_oow_t capacities[] = { 0, 1, 2, 4, 32 };
	hio_oow_t i, j, k, blen, byte_result, wide_result;
	int good;

	for (i = 0; i < HIO_COUNTOF(lengths); i++)
	{
		for (j = 0; j < HIO_COUNTOF(capacities); j++)
		{
			blen = capacities[j];
			for (k = 0; k < HIO_COUNTOF(bytes); k++) bytes[k] = 'X';
			for (k = 0; k < HIO_COUNTOF(wide); k++) wide[k] = 'X';
			byte_result = hio_ipad_bytes_to_bcstr(raw, lengths[i], bytes, blen);
			wide_result = hio_ipad_bytes_to_ucstr(raw, lengths[i], wide, blen);
			good = byte_result == 0 && wide_result == 0;
			for (k = 0; k < HIO_COUNTOF(bytes); k++)
			{
				if (bytes[k] != (blen > 0 && k == 0? '\0': 'X')) good = 0;
				if (wide[k] != (blen > 0 && k == 0? '\0': 'X')) good = 0;
			}
			OK(good, "unsupported address length produces an empty string without changing surrounding storage");
		}
	}
	OK(hio_ipad_bytes_to_bcstr(raw, 5, HIO_NULL, 0) == 0 &&
	   hio_ipad_bytes_to_ucstr(raw, 5, HIO_NULL, 0) == 0,
		"unsupported address length accepts a null output with zero capacity");
}

static void test_unaligned_loopback (void)
{
	union
	{
		hio_uint32_t align;
		hio_uint8_t bytes[24];
	} storage;
	hio_uint8_t addresses[][16] =
	{
		{ 127, 0, 0, 1 },
		{ 127, 255, 255, 255 },
		{ 126, 255, 255, 255 },
		{ 128, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
		{ 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 127, 0, 0, 1 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 127, 255, 255, 255 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 126, 255, 255, 255 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 128, 0, 0, 1 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 1 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 127, 0, 0, 1 },
		{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }
	};
	int expected[] = { 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0 };
	hio_oow_t i, offset, len;
	int good;

	for (i = 0; i < HIO_COUNTOF(addresses); i++)
	{
		len = i < 4? HIO_IP4AD_LEN: HIO_IP6AD_LEN;
		good = 1;
		for (offset = 0; offset < 8; offset++)
		{
			HIO_MEMSET(storage.bytes, 0xa5, HIO_SIZEOF(storage.bytes));
			HIO_MEMCPY(storage.bytes + offset, addresses[i], len);
			if (hio_ipad_bytes_is_loop_back(storage.bytes + offset, len) != expected[i]) good = 0;
			if (HIO_MEMCMP(storage.bytes + offset, addresses[i], len) != 0) good = 0;
		}
		OK(good, "loopback classification accepts aligned and unaligned IPv4 and IPv6 bytes without modifying them");
	}
}

static void test_ipv4_buffers (void)
{
	const hio_bch_t* inputs[] = { "0.0.0.0", "127.0.0.1", "192.0.2.1", "255.255.255.255" };
	hio_uint8_t expected[][4] = { { 0 }, { 127, 0, 0, 1 }, { 192, 0, 2, 1 }, { 255, 255, 255, 255 } };
	hio_oow_t capacities[] = { 0, 1, 3, 4, 5, 15, 16, 17, 32 };
	hio_uch_t wide[HIO_IP4AD_STRLEN + 1];
	hio_uint8_t byte_result[40], wide_result[40];
	hio_uint8_t value;
	hio_oow_t i, j, k, len, capa;
	int byte_len, wide_len, good;

	for (i = 0; i < HIO_COUNTOF(inputs); i++)
	{
		len = hio_count_bcstr(inputs[i]);
		for (j = 0; j < len; j++) wide[j] = inputs[i][j];
		for (j = 0; j < HIO_COUNTOF(capacities); j++)
		{
			capa = capacities[j];
			HIO_MEMSET(byte_result, 0xa5, HIO_SIZEOF(byte_result));
			HIO_MEMSET(wide_result, 0xa5, HIO_SIZEOF(wide_result));
			byte_len = hio_bchars_to_ipad_bytes(inputs[i], len, byte_result + 1, capa);
			wide_len = hio_uchars_to_ipad_bytes(wide, len, wide_result + 1, capa);
			good = byte_len == (capa >= 4? 4: -1) && wide_len == (capa >= 4? 4: -1);
			for (k = 0; k < HIO_COUNTOF(byte_result); k++)
			{
				value = capa >= 4 && k >= 1 && k <= 4? expected[i][k - 1]: 0xa5;
				if (byte_result[k] != value || wide_result[k] != value) good = 0;
			}
			OK(good, "IPv4 parsing accepts every sufficient buffer size and writes only four address bytes");
		}
	}
	OK(hio_bchars_to_ipad_bytes("127.0.0.1", 9, HIO_NULL, 0) == -1 &&
	   hio_uchars_to_ipad_bytes(wide_loopback, 9, HIO_NULL, 0) == -1,
		"IPv4 parsing with zero capacity does not access a null output");
}

static void test_cstr_macros (void)
{
	hio_uint8_t raw[16];
	hio_uint8_t expected[] = { 127, 0, 0, 1 };

	OK(hio_bcstr_to_ipad_bytes("127.0.0.1", raw, 4) == 4 &&
	   HIO_MEMCMP(raw, expected, 4) == 0,
		"byte C-string conversion macro supplies the string length and output arguments");
	OK(hio_ucstr_to_ipad_bytes(wide_loopback, raw, 4) == 4 &&
	   HIO_MEMCMP(raw, expected, 4) == 0,
		"wide C-string conversion macro supplies the string length and output arguments");
	OK(hio_oocstr_to_ipad_bytes(generic_loopback, raw, HIO_SIZEOF(raw)) == 4 &&
	   HIO_MEMCMP(raw, expected, 4) == 0,
		"generic C-string conversion macro selects the configured character type");
	OK(hio_bcstr_to_ipad_bytes("127.0.0.1", raw, 3) == -1 &&
	   hio_ucstr_to_ipad_bytes(wide_loopback, raw, 3) == -1 &&
	   hio_oocstr_to_ipad_bytes(generic_loopback, raw, 3) == -1,
		"C-string conversion macros honor the output capacity");
	OK(hio_bcstr_to_ipad_bytes("invalid", raw, HIO_SIZEOF(raw)) == -1 &&
	   hio_ucstr_to_ipad_bytes(wide_invalid, raw, HIO_SIZEOF(raw)) == -1 &&
	   hio_oocstr_to_ipad_bytes(generic_invalid, raw, HIO_SIZEOF(raw)) == -1,
		"C-string conversion macros preserve parsing failures");
}

static void test_address_suffixes (hio_t* hio, const hio_bch_t* address, int family, int port, int scope_id)
{
	struct
	{
		const hio_bch_t* ptr;
		hio_oow_t len;
	} suffixes[] =
	{
		{ "junk", 4 },
		{ " ", 1 },
		{ "\t", 1 },
		{ "\n", 1 },
		{ "\0junk", 5 },
		{ "]", 1 },
		{ ":81junk", 7 }
	};
	hio_bch_t bytes[96];
	hio_uch_t wide[96];
	hio_skad_t skad;
	hio_oow_t i, j, len, input_len;
	int byte_good, wide_good;

	len = hio_count_bcstr(address);
	for (i = 0; i < len; i++) wide[i] = bytes[i] = address[i];
	/* The parser must honor the supplied length, even without a terminator. */
	bytes[len] = '!';
	wide[len] = '!';
	byte_good = hio_bcharstoskad(hio, bytes, len, &skad) == 0 &&
		hio_skad_get_family(&skad) == family && hio_skad_get_port(&skad) == port &&
		hio_skad_get_scope_id(&skad) == scope_id;
	wide_good = hio_ucharstoskad(hio, wide, len, &skad) == 0 &&
		hio_skad_get_family(&skad) == family && hio_skad_get_port(&skad) == port &&
		hio_skad_get_scope_id(&skad) == scope_id;
	OK(byte_good && wide_good, "valid address slices preserve family, port, and scope without reading beyond their length");

	for (i = 0; i < HIO_COUNTOF(suffixes); i++)
	{
		for (j = 0; j < suffixes[i].len; j++) wide[len + j] = bytes[len + j] = suffixes[i].ptr[j];
		input_len = len + suffixes[i].len;
		hio_seterrnum(hio, HIO_ENOERR);
		byte_good = hio_bcharstoskad(hio, bytes, input_len, &skad) == -1 &&
			hio_geterrnum(hio) == HIO_EINVAL;
		hio_seterrnum(hio, HIO_ENOERR);
		wide_good = hio_ucharstoskad(hio, wide, input_len, &skad) == -1 &&
			hio_geterrnum(hio) == HIO_EINVAL;
		OK(byte_good && wide_good, "byte and wide address parsers reject trailing text, whitespace, NULs, and extra delimiters");
	}
}

static void test_interface_lookup_errors (hio_t* hio)
{
	hio_bch_t longname[512];
	hio_bch_t message[128];
	hio_uch_t wide[512];
	const hio_bch_t* names[3];
	hio_oow_t i, j, len;
	unsigned int index;
	int variant, n, good;

	for (i = 0; i < HIO_COUNTOF(longname) - 1; i++) longname[i] = 'a';
	longname[i] = '\0';
	names[0] = "";
	names[1] = "no-hio-if";
	names[2] = longname;
	for (i = 0; i < HIO_COUNTOF(names); i++)
	{
		len = hio_count_bcstr(names[i]);
		for (j = 0; j <= len; j++) wide[j] = names[i][j];
		for (variant = 0; variant < 4; variant++)
		{
			index = 12345;
			hio_seterrnum(hio, HIO_ENOERR);
			switch (variant)
			{
				case 0: n = hio_bcstrtoifindex(hio, names[i], &index); break;
				case 1: n = hio_bcharstoifindex(hio, names[i], len, &index); break;
				case 2: n = hio_ucstrtoifindex(hio, wide, &index); break;
				default: n = hio_ucharstoifindex(hio, wide, len, &index); break;
			}
			if (i == 1 && n >= 0)
			{
				skip("the requested test interface exists", 1);
				continue;
			}
			good = n <= -1 && index == 12345 && hio_geterrnum(hio) != HIO_ENOERR;
			if (!good)
			{
				hio_fmttobcstr(hio, message, HIO_COUNTOF(message), "name case=%lu variant=%d result=%d errnum=%d", (unsigned long)i, variant, n, (int)hio_geterrnum(hio));
				diag(message);
			}
			OK(good, "all interface-name lookup variants report invalid names without modifying the output index");
		}
	}
}

static void test_interface_name_nuls (hio_t* hio)
{
	struct
	{
		const hio_bch_t* text;
		hio_oow_t len;
	} cases[] =
	{
		{ "lo\0junk", HIO_SIZEOF("lo\0junk") - 1 },
		{ "\0lo", HIO_SIZEOF("\0lo") - 1 },
		{ "lo\0", HIO_SIZEOF("lo\0") - 1 },
		{ "l\0o", HIO_SIZEOF("l\0o") - 1 },
		{ "\0", 1 }
	};
	hio_bch_t name[128];
	hio_uch_t wide[128];
	hio_oow_t i, j, len;
	unsigned int index;
	hio_errnum_t errnum;
	int variant, n;

	for (i = 0; i < HIO_COUNTOF(cases); i++)
	{
		for (j = 0; j < cases[i].len; j++) wide[j] = cases[i].text[j];
		for (variant = 0; variant < 2; variant++)
		{
			index = 12345;
			hio_seterrnum(hio, HIO_ENOERR);
			if (variant == 0)
				n = hio_bcharstoifindex(hio, cases[i].text, cases[i].len, &index);
			else
				n = hio_ucharstoifindex(hio, wide, cases[i].len, &index);
			errnum = hio_geterrnum(hio);
			OK(n <= -1 && index == 12345 && (errnum == HIO_EINVAL || errnum == HIO_ENOIMPL),
				"direct length-based interface lookups reject embedded NULs without changing the output index");
		}
	}

	n = hio_ifindextobcstr(hio, 1, name, HIO_COUNTOF(name));
	if (n <= -1 || (hio_oow_t)n > HIO_COUNTOF(name) - 3)
	{
		skip("a suitable interface name is unavailable", 2);
		return;
	}
	len = n;
	for (i = 0; i < len; i++)
	{
		if ((hio_uint8_t)name[i] >= 128)
		{
			skip("interface name is not ASCII", 2);
			return;
		}
		wide[i] = name[i];
	}
	name[len] = wide[len] = '\0';
	name[len + 1] = wide[len + 1] = '!';
	index = 12345;
	OK(hio_bcharstoifindex(hio, name, len, &index) >= 0 && index == 1,
		"a NUL outside a byte interface-name slice does not invalidate that slice");
	index = 12345;
	OK(hio_ucharstoifindex(hio, wide, len, &index) >= 0 && index == 1,
		"a NUL outside a wide interface-name slice does not invalidate that slice");
}

static void test_reverse_lookup_errors (hio_t* hio)
{
	unsigned int indices[] = { 0, HIO_TYPE_MAX(unsigned int) };
	hio_bch_t bytes[32];
	hio_uch_t wide[32];
	hio_oow_t i, j;
	int variant, n, good;

	for (i = 0; i < HIO_COUNTOF(indices); i++)
	{
		for (variant = 0; variant < 2; variant++)
		{
			for (j = 0; j < HIO_COUNTOF(bytes); j++) bytes[j] = wide[j] = 'X';
			hio_seterrnum(hio, HIO_ENOERR);
			if (variant == 0)
				n = hio_ifindextobcstr(hio, indices[i], bytes, HIO_COUNTOF(bytes));
			else
				n = hio_ifindextoucstr(hio, indices[i], wide, HIO_COUNTOF(wide));
			good = n <= -1 && hio_geterrnum(hio) != HIO_ENOERR;
			for (j = 0; j < HIO_COUNTOF(bytes); j++)
			{
				if (bytes[j] != 'X' || wide[j] != 'X') good = 0;
			}
			OK(good, "invalid reverse lookups report a diagnostic and preserve both output buffers");
		}
	}
}

#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
static void test_scope_lookup_errors (hio_t* hio)
{
	const hio_bch_t* cases[] =
	{
		"fe80::1%no-hio-if",
		"[fe80::1%no-hio-if]",
		"[fe80::1%no-hio-if]:80",
		"fe80::1%abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
	};
	hio_skad_t skad;
	hio_uch_t wide[128];
	hio_oow_t i, j, len;
	int byte_result, wide_result, byte_good, wide_good;

	for (i = 0; i < HIO_COUNTOF(cases); i++)
	{
		len = hio_count_bcstr(cases[i]);
		for (j = 0; j < len; j++) wide[j] = cases[i][j];
		hio_seterrnum(hio, HIO_ENOERR);
		byte_result = hio_bcharstoskad(hio, cases[i], len, &skad);
		byte_good = byte_result <= -1 && hio_geterrnum(hio) != HIO_ENOERR;
		hio_seterrnum(hio, HIO_ENOERR);
		wide_result = hio_ucharstoskad(hio, wide, len, &skad);
		wide_good = wide_result <= -1 && hio_geterrnum(hio) != HIO_ENOERR;
		if (byte_result >= 0 && wide_result >= 0)
		{
			skip("the requested test interface exists", 1);
			continue;
		}
		OK(byte_good && wide_good, "bare and bracketed IPv6 scopes preserve interface lookup failure diagnostics");
	}
}

static void test_scope_embedded_nuls (hio_t* hio)
{
	struct
	{
		const hio_bch_t* text;
		hio_oow_t len;
	} cases[] =
	{
		{ "fe80::1%lo\0junk", HIO_SIZEOF("fe80::1%lo\0junk") - 1 },
		{ "[fe80::1%lo\0junk]", HIO_SIZEOF("[fe80::1%lo\0junk]") - 1 },
		{ "[fe80::1%lo\0junk]:80", HIO_SIZEOF("[fe80::1%lo\0junk]:80") - 1 },
		{ "fe80::1%\0lo", HIO_SIZEOF("fe80::1%\0lo") - 1 },
		{ "[fe80::1%\0lo]:80", HIO_SIZEOF("[fe80::1%\0lo]:80") - 1 },
		{ "fe80::1%lo\0", HIO_SIZEOF("fe80::1%lo\0") - 1 },
		{ "[fe80::1%lo\0]:80", HIO_SIZEOF("[fe80::1%lo\0]:80") - 1 }
	};
	hio_skad_t skad;
	hio_uch_t wide[32];
	hio_oow_t i, j;
	int byte_good, wide_good;

	for (i = 0; i < HIO_COUNTOF(cases); i++)
	{
		for (j = 0; j < cases[i].len; j++) wide[j] = cases[i].text[j];
		hio_seterrnum(hio, HIO_ENOERR);
		byte_good = hio_bcharstoskad(hio, cases[i].text, cases[i].len, &skad) == -1 &&
			hio_geterrnum(hio) == HIO_EINVAL;
		hio_seterrnum(hio, HIO_ENOERR);
		wide_good = hio_ucharstoskad(hio, wide, cases[i].len, &skad) == -1 &&
			hio_geterrnum(hio) == HIO_EINVAL;
		OK(byte_good && wide_good, "interface-name scopes reject embedded NULs before OS lookup in both character widths");
	}
}

static void test_scope_output_lengths (hio_t* hio)
{
	hio_skad_t skad;
	hio_bch_t name[128], input[160], bytes[164];
	hio_uch_t source[160], wide[164];
	hio_oow_t i, j, namelen, inputlen, byte_len, wide_len, cap, expected;
	unsigned int index = 1;
	int n, good, flags;

	n = hio_ifindextobcstr(hio, index, name, HIO_COUNTOF(name));
	if (n < 2)
	{
		skip("an interface with a name of at least two characters is unavailable", 3);
		return;
	}
	namelen = n;
	for (i = 0; i < namelen; i++)
	{
		if ((hio_uint8_t)name[i] >= 128)
		{
			skip("interface name is not ASCII", 3);
			return;
		}
	}
	inputlen = hio_copy_bcstr(input, HIO_COUNTOF(input), "[fe80::1%");
	inputlen += hio_copy_bcstr(input + inputlen, HIO_COUNTOF(input) - inputlen, name);
	inputlen += hio_copy_bcstr(input + inputlen, HIO_COUNTOF(input) - inputlen, "]:80");
	for (i = 0; i < inputlen; i++) source[i] = input[i];
	source[inputlen] = '!'; /* A terminator outside the input slice is unnecessary. */
	good = hio_bcharstoskad(hio, input, inputlen, &skad) == 0 &&
		(hio_uint32_t)hio_skad_get_scope_id(&skad) == index &&
		hio_ucharstoskad(hio, source, inputlen, &skad) == 0 &&
		(hio_uint32_t)hio_skad_get_scope_id(&skad) == index;
	OK(good, "valid byte and wide interface-name scopes preserve the interface index");
	if (!good)
	{
		skip("cannot parse the selected interface name", 2);
		return;
	}

	good = 1;
	for (cap = 2; cap <= namelen + 2; cap++)
	{
		for (i = 0; i < HIO_COUNTOF(wide); i++) wide[i] = 'X';
		n = hio_ifindextoucstr(hio, index, wide + 1, cap);
		expected = namelen < cap? namelen: cap - 1;
		if (n <= -1 || (hio_oow_t)n != expected || wide[expected + 1] != '\0') good = 0;
		for (i = 0; i < expected; i++) if (wide[i + 1] != (hio_uch_t)name[i]) good = 0;
		if (wide[0] != 'X' || wide[cap + 1] != 'X') good = 0;
	}
	OK(good, "wide interface-name truncation returns the character count excluding its terminator");

	flags = HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT;
	good = 1;
	for (cap = 0; cap <= inputlen + 3; cap++)
	{
		for (i = 0; i < HIO_COUNTOF(bytes); i++) bytes[i] = wide[i] = 'X';
		byte_len = hio_skadtobcstr(hio, &skad, bytes + 1, cap, flags);
		wide_len = hio_skadtoucstr(hio, &skad, wide + 1, cap, flags);
		if (byte_len != wide_len) good = 0;
		if (cap > 0)
		{
			if (byte_len >= cap || wide_len >= cap ||
			    hio_count_bcstr_limited(bytes + 1, cap) != byte_len ||
			    hio_count_ucstr_limited(wide + 1, cap) != wide_len) good = 0;
		}
		for (j = 0; j < cap; j++) if (wide[j + 1] != (hio_uch_t)bytes[j + 1]) good = 0;
		if (bytes[0] != 'X' || wide[0] != 'X' || bytes[cap + 1] != 'X' || wide[cap + 1] != 'X') good = 0;
	}
	OK(good, "byte and wide scoped-address formatting agree on output and lengths at every buffer boundary");
}

static void test_scope_descriptor_cleanup (hio_t* hio)
{
#if defined(HAVE_UNISTD_H) && !defined(_WIN32) && !defined(__OS2__) && !defined(__DOS__)
	hio_uch_t name[512], address[544];
	hio_bch_t ifname[128];
	hio_skad_t skad;
	const hio_bch_t* prefix = "fe80::1%";
	hio_oow_t i, j, len;
	unsigned int index;
	int marker[2], before, after, form, repeat, good;

	if (hio_ifindextobcstr(hio, 1, ifname, HIO_COUNTOF(ifname)) <= -1 || pipe(marker) <= -1)
	{
		skip("interface lookup or descriptor probes are unavailable", 1);
		return;
	}
	before = dup(marker[0]);
	if (before <= -1)
	{
		close(marker[0]);
		close(marker[1]);
		skip("descriptor duplication is unavailable", 1);
		return;
	}
	close(before);
	for (i = 0; i < HIO_COUNTOF(name) - 1; i++) name[i] = 'a';
	name[i] = '\0';
	good = 1;
	for (repeat = 0; repeat < 10; repeat++)
	{
		if (hio_ucstrtoifindex(hio, name, &index) >= 0) good = 0;
		if (hio_ucharstoifindex(hio, name, HIO_COUNTOF(name) - 1, &index) >= 0) good = 0;
		for (form = 0; form < 2; form++)
		{
			len = 0;
			if (form) address[len++] = '[';
			for (j = 0; prefix[j]; j++) address[len++] = prefix[j];
			for (j = 0; j < HIO_COUNTOF(name) - 1; j++) address[len++] = name[j];
			if (form) address[len++] = ']';
			if (hio_ucharstoskad(hio, address, len, &skad) >= 0) good = 0;
		}
	}
	after = dup(marker[0]);
	if (after >= 0) close(after);
	close(marker[0]);
	close(marker[1]);
	OK(good && after == before, "rejected wide names and bracketed or bare IPv6 scopes do not leak descriptors");
#else
	skip("descriptor probes are unavailable", 1);
#endif
}
#endif

#if defined(AF_UNIX) && (HIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
static void test_unix_embedded_nuls (hio_t* hio)
{
	struct
	{
		const hio_bch_t* text;
		hio_oow_t len;
	} cases[] =
	{
		{ "@/tmp/b\0ignored", HIO_SIZEOF("@/tmp/b\0ignored") - 1 },
		{ "@/tmp/a\0ignored", HIO_SIZEOF("@/tmp/a\0ignored") - 1 },
		{ "@relative\0ignored", HIO_SIZEOF("@relative\0ignored") - 1 },
		{ "@/tmp/a\0", HIO_SIZEOF("@/tmp/a\0") - 1 },
		{ "@\0a", HIO_SIZEOF("@\0a") - 1 }
	};
	union { hio_skad_t public_address; hio_skad_alt_t native; } storage;
	hio_skad_alt_t* native = &storage.native;
	hio_uch_t wide[32];
	hio_oow_t i, j, prefixlen;
	int byte_good, wide_good;

	for (i = 0; i < HIO_COUNTOF(cases); i++)
	{
		for (j = 0; j < cases[i].len; j++) wide[j] = cases[i].text[j];
		hio_seterrnum(hio, HIO_ENOERR);
		byte_good = hio_bcharstoskad(hio, cases[i].text, cases[i].len, &storage.public_address) == -1 &&
			hio_geterrnum(hio) == HIO_EINVAL;
		hio_seterrnum(hio, HIO_ENOERR);
		wide_good = hio_ucharstoskad(hio, wide, cases[i].len, &storage.public_address) == -1 &&
			hio_geterrnum(hio) == HIO_EINVAL;
		OK(byte_good && wide_good, "Unix pathname parsers reject NULs within the supplied input length");

		prefixlen = hio_count_bcstr(cases[i].text);
		byte_good = hio_bcharstoskad(hio, cases[i].text, prefixlen, &storage.public_address) == 0 &&
			native->un.sun_family == AF_UNIX;
		wide_good = hio_ucharstoskad(hio, wide, prefixlen, &storage.public_address) == 0 &&
			native->un.sun_family == AF_UNIX;
		OK(byte_good && wide_good, "a NUL outside the supplied Unix pathname slice does not invalidate the slice");
	}
}

static void test_unix_read_bounds (hio_t* hio)
{
	enum { PATH_CAPA = HIO_SIZEOF(((struct sockaddr_un*)0)->sun_path) };
	hio_skad_t* a;
	hio_skad_t* b;
	struct sockaddr_un* un_a;
	struct sockaddr_un* un_b;
	hio_bch_t bytes[PATH_CAPA + 10];
	hio_uch_t wide[PATH_CAPA + 10];
	hio_bch_t expected[PATH_CAPA + 2];
	hio_oow_t capacities[] = { 0, 1, 2, PATH_CAPA / 2, PATH_CAPA + 1, PATH_CAPA + 8 };
	hio_oow_t i, j, n, byte_len, wide_len, alloc_size;
	int prefix, flags, good;
	hio_uch_t value;

	/* Different suffixes outside sun_path expose unbounded reads without
	 * requiring a crash. The final allocation byte terminates the suffix. */
	alloc_size = HIO_SIZEOF(*a) + 8;
	a = hio_allocmem(hio, alloc_size);
	b = hio_allocmem(hio, alloc_size);
	if (!a || !b) { bail_out("cannot allocate native Unix addresses"); return; }
	HIO_MEMSET(a, 'A', alloc_size);
	HIO_MEMSET(b, 'B', alloc_size);
	HIO_MEMSET(a, 0, HIO_OFFSETOF(struct sockaddr_un, sun_path));
	HIO_MEMSET(b, 0, HIO_OFFSETOF(struct sockaddr_un, sun_path));
	((hio_uint8_t*)a)[alloc_size - 1] = '\0';
	((hio_uint8_t*)b)[alloc_size - 1] = '\0';
	un_a = (struct sockaddr_un*)a;
	un_b = (struct sockaddr_un*)b;
	un_a->sun_family = AF_UNIX;
	un_b->sun_family = AF_UNIX;
	HIO_MEMSET(un_a->sun_path, 'a', PATH_CAPA);
	HIO_MEMSET(un_b->sun_path, 'a', PATH_CAPA);
	un_a->sun_path[0] = '/';
	un_b->sun_path[0] = '/';

	for (prefix = 1; prefix < 2; prefix++)
	{
		flags = HIO_SKAD_TO_BCSTR_ADDR;
		if (prefix) expected[0] = '@';
		HIO_MEMCPY(expected + prefix, un_a->sun_path, PATH_CAPA);
		for (i = 0; i < HIO_COUNTOF(capacities); i++)
		{
			for (j = 0; j < HIO_COUNTOF(bytes); j++) wide[j] = bytes[j] = '!';
			byte_len = hio_skadtobcstr(hio, a, bytes, capacities[i], flags);
			wide_len = hio_skadtoucstr(hio, a, wide, capacities[i], flags);
			n = capacities[i] == 0? 0: capacities[i] - 1;
			if (n > PATH_CAPA + prefix) n = PATH_CAPA + prefix;
			good = byte_len == n && wide_len == n;
			for (j = 0; j < HIO_COUNTOF(bytes); j++)
			{
				value = j < n? expected[j]: (capacities[i] > 0 && j == n? '\0': '!');
				if (bytes[j] != value || wide[j] != value) good = 0;
			}
			OK(good, "Unix formatting bounds reads by sun_path and terminates every nonempty output buffer");
		}
	}
	OK(hio_equal_skads(a, b, 0) && hio_equal_skads(b, a, 1),
		"full unterminated Unix paths compare independently of bytes following sun_path");
	un_b->sun_path[PATH_CAPA - 1] = 'b';
	OK(!hio_equal_skads(a, b, 0) && !hio_equal_skads(b, a, 1),
		"Unix comparison includes the last byte of a full unterminated path");
	HIO_MEMSET(un_b->sun_path, 'b', PATH_CAPA);
	HIO_MEMCPY(un_a->sun_path, "/tmp/a", HIO_SIZEOF("/tmp/a"));
	HIO_MEMCPY(un_b->sun_path, "/tmp/a", HIO_SIZEOF("/tmp/a"));
	OK(hio_equal_skads(a, b, 0) && hio_equal_skads(b, a, 1),
		"terminated Unix paths compare independently of unused bytes after the terminator");
	hio_freemem(hio, a);
	hio_freemem(hio, b);
}

static void test_unix_path_limits (hio_t* hio)
{
	enum { PATH_CAPA = HIO_SIZEOF(((struct sockaddr_un*)0)->sun_path) };
	hio_bch_t bytes[PATH_CAPA * 2 + 2];
	hio_uch_t wide[PATH_CAPA * 2 + 2];
	hio_oow_t lengths[] = { 1, PATH_CAPA - 1, PATH_CAPA, PATH_CAPA + 1, PATH_CAPA * 2 };
	union { hio_skad_t public_address; hio_skad_alt_t native; } storage;
	hio_skad_alt_t* native = &storage.native;
	hio_oow_t i, j, pathlen, prefixlen, len;
	int form, result, byte_good, wide_good;

	/* @-prefixed absolute and relative paths. */
	for (form = 1; form < 3; form++)
	{
		for (i = 0; i < HIO_COUNTOF(lengths); i++)
		{
			pathlen = lengths[i];
			prefixlen = form > 0? 1: 0;
			if (prefixlen > 0) bytes[0] = '@';
			for (j = 0; j < pathlen; j++) bytes[prefixlen + j] = 'a';
			if (form != 2) bytes[prefixlen] = '/';
			len = prefixlen + pathlen;
			for (j = 0; j < len; j++) wide[j] = bytes[j];
			bytes[len] = '!';
			wide[len] = '!';

			hio_seterrnum(hio, HIO_ENOERR);
			result = hio_bcharstoskad(hio, bytes, len, &storage.public_address);
			if (pathlen < PATH_CAPA)
			{
				byte_good = result == 0 && native->un.sun_family == AF_UNIX &&
					HIO_MEMCMP(native->un.sun_path, bytes + prefixlen, pathlen) == 0 &&
					native->un.sun_path[pathlen] == '\0';
			}
			else byte_good = result == -1 && hio_geterrnum(hio) == HIO_EBUFFULL;

			hio_seterrnum(hio, HIO_ENOERR);
			result = hio_ucharstoskad(hio, wide, len, &storage.public_address);
			if (pathlen < PATH_CAPA)
			{
				wide_good = result == 0 && native->un.sun_family == AF_UNIX &&
					HIO_MEMCMP(native->un.sun_path, bytes + prefixlen, pathlen) == 0 &&
					native->un.sun_path[pathlen] == '\0';
			}
			else wide_good = result == -1 && hio_geterrnum(hio) == HIO_EBUFFULL;
			OK(byte_good && wide_good, pathlen < PATH_CAPA?
				"Unix paths fitting sun_path retain every byte and their terminator":
				"Unix paths exceeding sun_path fail without silently truncating the path");
		}
	}
}
#endif

#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
static void test_scope_id_limits (hio_t* hio)
{
	struct
	{
		const hio_bch_t* text;
		int valid;
		hio_uint32_t value;
	} cases[] =
	{
		{ "0", 1, 0 },
		{ "1", 1, 1 },
		{ "4294967294", 1, HIO_TYPE_MAX(hio_uint32_t) - 1 },
		{ "4294967295", 1, HIO_TYPE_MAX(hio_uint32_t) },
		{ "00000000004294967295", 1, HIO_TYPE_MAX(hio_uint32_t) },
		{ "4294967296", 0, 0 },
		{ "4772185884", 0, 0 },
		{ "5000000000", 0, 0 },
		{ "8589934591", 0, 0 },
		{ "10000000000", 0, 0 },
		{ "18446744073709551615", 0, 0 },
		{ "00000000005000000000", 0, 0 }
	};
	const hio_bch_t* prefix = "fe80::1%";
	hio_bch_t bytes[96];
	hio_uch_t wide[96];
	hio_skad_t skad;
	hio_oow_t i, j, len;
	int form, result, byte_good, wide_good;

	/* Cover bare IPv6, brackets, and brackets followed by a port. */
	for (form = 0; form < 3; form++)
	{
		for (i = 0; i < HIO_COUNTOF(cases); i++)
		{
			len = 0;
			if (form > 0) bytes[len++] = '[';
			for (j = 0; prefix[j] != '\0'; j++) bytes[len++] = prefix[j];
			for (j = 0; cases[i].text[j] != '\0'; j++) bytes[len++] = cases[i].text[j];
			if (form > 0) bytes[len++] = ']';
			if (form == 2)
			{
				bytes[len++] = ':';
				bytes[len++] = '8';
				bytes[len++] = '0';
			}
			for (j = 0; j < len; j++) wide[j] = bytes[j];

			hio_seterrnum(hio, HIO_ENOERR);
			result = hio_bcharstoskad(hio, bytes, len, &skad);
			if (cases[i].valid)
			{
				byte_good = result == 0 && hio_skad_get_family(&skad) == HIO_AF_INET6 &&
					(hio_uint32_t)hio_skad_get_scope_id(&skad) == cases[i].value &&
					hio_skad_get_port(&skad) == (form == 2? 80: 0);
			}
			else byte_good = result == -1 && hio_geterrnum(hio) == HIO_EINVAL;

			hio_seterrnum(hio, HIO_ENOERR);
			result = hio_ucharstoskad(hio, wide, len, &skad);
			if (cases[i].valid)
			{
				wide_good = result == 0 && hio_skad_get_family(&skad) == HIO_AF_INET6 &&
					(hio_uint32_t)hio_skad_get_scope_id(&skad) == cases[i].value &&
					hio_skad_get_port(&skad) == (form == 2? 80: 0);
			}
			else wide_good = result == -1 && hio_geterrnum(hio) == HIO_EINVAL;
			OK(byte_good && wide_good, "numeric scope IDs accept the full uint32 range and reject overflow before arithmetic");
		}
	}
}

static void test_excess_groups (hio_t* hio)
{
	/* Extra storage lets the old implementation report failed assertions
	 * instead of corrupting unrelated memory. Guards surround the public
	 * address object, including any native padding. */
	union
	{
		hio_uintmax_t align;
		hio_uint8_t bytes[256 + HIO_SIZEOF(hio_skad_t) + 256];
	} storage;
	hio_skad_t* skad = (hio_skad_t*)(storage.bytes + 256);
	hio_bch_t bytes[132];
	hio_uch_t wide[132];
	hio_oow_t counts[] = { 9, 10, 64 };
	hio_oow_t i, j, len;
	int compressed, byte_result, wide_result, byte_guard, wide_guard;

	for (compressed = 0; compressed < 2; compressed++)
	{
		for (i = 0; i < HIO_COUNTOF(counts); i++)
		{
			len = 0;
			if (compressed)
			{
				bytes[len++] = ':';
				bytes[len++] = ':';
			}
			for (j = 0; j < counts[i]; j++)
			{
				if (j > 0) bytes[len++] = ':';
				bytes[len++] = '1';
			}
			for (j = 0; j < len; j++) wide[j] = bytes[j];

			HIO_MEMSET(storage.bytes, 0xa5, HIO_SIZEOF(storage.bytes));
			byte_result = hio_bcharstoskad(hio, bytes, len, skad);
			byte_guard = 1;
			for (j = 0; j < HIO_SIZEOF(storage.bytes); j++)
			{
				if ((j < 256 || j >= 256 + HIO_SIZEOF(*skad)) && storage.bytes[j] != 0xa5) byte_guard = 0;
			}

			HIO_MEMSET(storage.bytes, 0xa5, HIO_SIZEOF(storage.bytes));
			wide_result = hio_ucharstoskad(hio, wide, len, skad);
			wide_guard = 1;
			for (j = 0; j < HIO_SIZEOF(storage.bytes); j++)
			{
				if ((j < 256 || j >= 256 + HIO_SIZEOF(*skad)) && storage.bytes[j] != 0xa5) wide_guard = 0;
			}
			OK(byte_result == -1 && wide_result == -1,
				"byte and wide parsers reject excess IPv6 groups with and without compression");
			OK(byte_guard && wide_guard, "excess IPv6 groups cannot overwrite the address object's guard");
		}
	}
}

static void test_invalid_ipv6_syntax (hio_t* hio)
{
	const hio_bch_t* inputs[] =
	{
		"1::2:3:4:5:6:7:8", "::1:2:3:4:5:6:7:8", "1:2:3:4:5:6:7:8::",
		"1:2:3:4:5:6::192.0.2.1", "::1:2:3:4:5:6:192.0.2.1",
		"00000:1:2:3:4:5:6:7", "1:2:3:4:5:6:7:00000", "1:2:00001::3",
		"::00000", "00000000000000000000000000000000::",
		"1::2::3", ":::1", "1:2:3:4:5:6:7:", "1:2:3:4:5:6:7"
	};
	hio_bch_t bytes[128];
	hio_uch_t wide[128];
	hio_uint8_t raw[16];
	hio_skad_t skad;
	hio_oow_t i, j, len, bare_len;
	int form, byte_good, wide_good;

	for (i = 0; i < HIO_COUNTOF(inputs); i++)
	{
		bare_len = hio_count_bcstr(inputs[i]);
		for (j = 0; j < bare_len; j++) wide[j] = inputs[i][j];
		byte_good = hio_bchars_to_ipad_bytes(inputs[i], bare_len, raw, HIO_SIZEOF(raw)) == -1;
		wide_good = hio_uchars_to_ipad_bytes(wide, bare_len, raw, HIO_SIZEOF(raw)) == -1;
		OK(byte_good && wide_good, "raw IPv6 parsers reject empty compression, overlong groups, and malformed separators");

		for (form = 0; form < 3; form++)
		{
			len = 0;
			if (form > 0) bytes[len++] = '[';
			for (j = 0; j < bare_len; j++) bytes[len++] = inputs[i][j];
			if (form > 0) bytes[len++] = ']';
			if (form == 2)
			{
				bytes[len++] = ':';
				bytes[len++] = '8';
				bytes[len++] = '0';
			}
			for (j = 0; j < len; j++) wide[j] = bytes[j];
			hio_seterrnum(hio, HIO_ENOERR);
			byte_good = hio_bcharstoskad(hio, bytes, len, &skad) == -1 &&
				hio_geterrnum(hio) == HIO_EINVAL;
			hio_seterrnum(hio, HIO_ENOERR);
			wide_good = hio_ucharstoskad(hio, wide, len, &skad) == -1 &&
				hio_geterrnum(hio) == HIO_EINVAL;
			OK(byte_good && wide_good, "socket-address parsers reject malformed IPv6 in bare and bracketed forms");
		}
	}
}

static void test_valid_ipv6 (hio_t* hio)
{
	const hio_bch_t* inputs[] =
	{
		"::", "::1", "2001:db8::1", "1:2:3:4:5:6:7:8",
		"::ffff:192.0.2.1", "::192.0.2.1",
		"1:2:3:4:5:6:192.0.2.1", "2001:db8::192.0.2.1",
		"::1:2:3:4:5:6:7", "1:2:3:4:5:6:7::", "1:2:3::4:5:6:7",
		"0000:0001:0002:0003:0004:0005:0006:ffff", "1:2:3:4:5::192.0.2.1"
	};
	const hio_bch_t* canonical[] =
	{
		"::", "::1", "2001:db8::1", "1:2:3:4:5:6:7:8",
		"::ffff:192.0.2.1", "::192.0.2.1",
		"1:2:3:4:5:6:c000:201", "2001:db8::c000:201",
		"0:1:2:3:4:5:6:7", "1:2:3:4:5:6:7:0", "1:2:3:0:4:5:6:7",
		"0:1:2:3:4:5:6:ffff", "1:2:3:4:5:0:c000:201"
	};
	hio_uint8_t expected[][16] =
	{
		{ 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
		{ 0x20, 1, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
		{ 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 0, 2, 1 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 0, 2, 1 },
		{ 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 192, 0, 2, 1 },
		{ 0x20, 1, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 192, 0, 2, 1 },
		{ 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7 },
		{ 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 0 },
		{ 0, 1, 0, 2, 0, 3, 0, 0, 0, 4, 0, 5, 0, 6, 0, 7 },
		{ 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 255, 255 },
		{ 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 0, 192, 0, 2, 1 }
	};
	hio_skad_t skad;
	hio_uint8_t raw[16];
	hio_uch_t wide[HIO_IP6AD_STRLEN + 1];
	hio_bch_t rendered[64];
	hio_oow_t i, j, len;
	int byte_good, wide_good;

	for (i = 0; i < HIO_COUNTOF(inputs); i++)
	{
		len = hio_count_bcstr(inputs[i]);
		for (j = 0; j < len; j++) wide[j] = inputs[i][j];
		byte_good = hio_bcharstoskad(hio, inputs[i], len, &skad) == 0 &&
			hio_skad_get_family(&skad) == HIO_AF_INET6 &&
			hio_skad_get_ipad_bytes(&skad, raw, HIO_SIZEOF(raw)) == 16 &&
			HIO_MEMCMP(raw, expected[i], 16) == 0;
		wide_good = hio_ucharstoskad(hio, wide, len, &skad) == 0 &&
			hio_skad_get_family(&skad) == HIO_AF_INET6 &&
			hio_skad_get_ipad_bytes(&skad, raw, HIO_SIZEOF(raw)) == 16 &&
			HIO_MEMCMP(raw, expected[i], 16) == 0;
		OK(byte_good && wide_good, "valid IPv6 addresses, including IPv4 tails, retain all address bytes");
		OK(hio_bchars_to_ipad_bytes(inputs[i], len, raw, HIO_SIZEOF(raw)) == 16 &&
		   HIO_MEMCMP(raw, expected[i], 16) == 0 &&
		   hio_uchars_to_ipad_bytes(wide, len, raw, HIO_SIZEOF(raw)) == 16 &&
		   HIO_MEMCMP(raw, expected[i], 16) == 0,
			"byte and wide raw-address parsers preserve all sixteen IPv6 bytes");
		OK(hio_ipad_bytes_to_bcstr(expected[i], 16, rendered, HIO_COUNTOF(rendered)) == hio_count_bcstr(canonical[i]) &&
		   hio_comp_bcstr(rendered, canonical[i], 0) == 0,
			"valid IPv6 byte-to-string conversion remains unchanged");
	}
}
#endif

static void test_qx_slices (hio_t* hio)
{
	const hio_bch_t* cases[] = { "", "<", "<q", "<qx", "<qx>", "<qx>junk", "<qx>\0" };
	hio_oow_t lengths[] = { 0, 1, 2, 3, 4, 8, 5 };
	hio_bch_t* bytes;
	hio_uch_t* wide;
	hio_skad_t address;
	hio_bch_t output[12];
	hio_uch_t wide_output[12];
	hio_oow_t i, j, len, capa, expected, byte_len, wide_len;
	int byte_good, wide_good;

	for (i = 0; i < HIO_COUNTOF(cases); i++)
	{
		len = lengths[i];
		/* Exact allocations expose any read beyond the explicit input slice. */
		bytes = hio_allocmem(hio, len > 0? len: 1);
		wide = hio_allocmem(hio, (len > 0? len: 1) * HIO_SIZEOF(*wide));
		if (!bytes || !wide)
		{
			if (bytes) hio_freemem(hio, bytes);
			if (wide) hio_freemem(hio, wide);
			bail_out("cannot allocate address slices");
			return;
		}
		for (j = 0; j < len; j++) wide[j] = bytes[j] = cases[i][j];
		byte_good = hio_bcharstoskad(hio, bytes, len, &address) == (len == 4? 0: -1);
		if (len == 4) byte_good = byte_good && hio_skad_get_family(&address) == HIO_AF_QX;
		wide_good = hio_ucharstoskad(hio, wide, len, &address) == (len == 4? 0: -1);
		if (len == 4) wide_good = wide_good && hio_skad_get_family(&address) == HIO_AF_QX;
		OK(byte_good && wide_good, "QX recognition requires exactly four characters and reads only within the input slice");
		hio_freemem(hio, bytes);
		hio_freemem(hio, wide);
	}

	hio_skad_init_for_qx(&address);
	for (capa = 0; capa <= 8; capa++)
	{
		for (i = 0; i < HIO_COUNTOF(output); i++) output[i] = wide_output[i] = '!';
		byte_len = hio_skadtobcstr(hio, &address, output + 1, capa, HIO_SKAD_TO_BCSTR_ADDR);
		wide_len = hio_skadtoucstr(hio, &address, wide_output + 1, capa, HIO_SKAD_TO_UCSTR_ADDR);
		expected = capa == 0? 0: capa - 1;
		if (expected > 4) expected = 4;
		byte_good = byte_len == expected && wide_len == expected;
		for (i = 0; i < HIO_COUNTOF(output); i++)
		{
			int value;
			value = i >= 1 && i <= expected? "<qx>"[i - 1]: (capa > 0 && i == expected + 1? '\0': '!');
			if (output[i] != value || wide_output[i] != value) byte_good = 0;
		}
		OK(byte_good, "QX formatting preserves its syntax and respects byte and wide output boundaries");
	}
}

static void test_sctp_metadata (hio_t* hio)
{
	hio_skad_t address;
	hio_skad_t copy;
	hio_bch_t output[80];
	hio_uch_t wide[80];
	hio_oow_t i, len;
	int good;

	OK(hio_bcstrtoskad(hio, "192.0.2.1:80", &address) == 0, "IPv4 parsing initializes Hio's extended address representation");
	hio_skad_set_chan(&address, 1234);
	hio_skad_set_ppid(&address, 0x89abcdefu);
	hio_skad_set_assoc(&address, -123);
	copy = address;
	OK(hio_skad_get_chan(&copy) == 1234 && hio_skad_get_ppid(&copy) == 0x89abcdefu &&
	   hio_skad_get_assoc(&copy) == -123 && hio_skad_get_size(&copy) == HIO_SIZEOF(struct sockaddr_in),
		"aligned public storage retains IPv4 SCTP metadata while the native address size excludes it");
	len = hio_skadtobcstr(hio, &copy, output, HIO_COUNTOF(output), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
	good = len == hio_count_bcstr("192.0.2.1:80") && hio_comp_bcstr(output, "192.0.2.1:80", 0) == 0;
	OK(good, "SCTP metadata does not change IPv4 address formatting");
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	OK(hio_bcstrtoskad(hio, "[2001:db8::1]:443", &address) == 0, "IPv6 parsing initializes Hio's extended address representation");
	hio_skad_set_chan(&address, 65535);
	hio_skad_set_ppid(&address, 0xfedcba98u);
	hio_skad_set_assoc(&address, 123456);
	copy = address;
	OK(hio_skad_get_chan(&copy) == 65535 && hio_skad_get_ppid(&copy) == 0xfedcba98u &&
	   hio_skad_get_assoc(&copy) == 123456 && hio_skad_get_size(&copy) == HIO_SIZEOF(struct sockaddr_in6),
		"aligned public storage retains IPv6 SCTP metadata while the native address size excludes it");
	len = hio_skadtobcstr(hio, &copy, output, HIO_COUNTOF(output), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
	good = len == hio_count_bcstr("[2001:db8::1]:443") && hio_comp_bcstr(output, "[2001:db8::1]:443", 0) == 0;
	if (hio_skadtoucstr(hio, &copy, wide, HIO_COUNTOF(wide), HIO_SKAD_TO_UCSTR_ADDR | HIO_SKAD_TO_UCSTR_PORT) != len) good = 0;
	for (i = 0; i <= len; i++) if (wide[i] != output[i]) good = 0;
	OK(good, "SCTP metadata does not change byte or wide IPv6 address formatting");
#else
	(void)i;
	(void)wide;
	skip("IPv6 addresses are unavailable", 3);
#endif
}

static void test_interface_descriptor_cleanup (hio_t* hio)
{
#if defined(HAVE_UNISTD_H) && !defined(_WIN32) && !defined(__OS2__) && !defined(__DOS__)
	hio_bch_t bytes[512];
	hio_uch_t wide[512];
	hio_oow_t i;
	unsigned int index;
	int marker[2], before, after, repeat, good;

	if (pipe(marker) <= -1)
	{
		skip("descriptor probes are unavailable", 1);
		return;
	}
	before = dup(marker[0]);
	if (before <= -1)
	{
		close(marker[0]);
		close(marker[1]);
		skip("descriptor duplication is unavailable", 1);
		return;
	}
	close(before);
	for (i = 0; i < HIO_COUNTOF(bytes) - 1; i++) bytes[i] = wide[i] = 'a';
	bytes[i] = wide[i] = '\0';
	good = 1;
	for (repeat = 0; repeat < 10; repeat++)
	{
		index = 12345;
		if (hio_bcstrtoifindex(hio, bytes, &index) >= 0) good = 0;
		if (hio_bcharstoifindex(hio, bytes, HIO_COUNTOF(bytes) - 1, &index) >= 0) good = 0;
		if (hio_ucstrtoifindex(hio, wide, &index) >= 0) good = 0;
		if (hio_ucharstoifindex(hio, wide, HIO_COUNTOF(wide) - 1, &index) >= 0) good = 0;
		if (index != 12345) good = 0;
	}
	after = dup(marker[0]);
	if (after >= 0) close(after);
	close(marker[0]);
	close(marker[1]);
	OK(good && after == before, "rejected byte and wide interface names leave no descriptors open");
#else
	skip("descriptor probes are unavailable", 1);
#endif
}

int main (void)
{
	hio_t* hio;
	hio_uint8_t ip4[] = { 192, 0, 2, 1 };
	hio_bch_t bytes[16];
	hio_uch_t wide[16];

	no_plan();
	test_native_alignment();
	test_invalid_lengths();
	test_unaligned_loopback();
	test_ipv4_buffers();
	test_cstr_macros();
	OK(hio_ipad_bytes_to_bcstr(ip4, 4, bytes, HIO_COUNTOF(bytes)) == 9 &&
	   hio_comp_bcstr(bytes, "192.0.2.1", 0) == 0 &&
	   hio_ipad_bytes_to_ucstr(ip4, 4, wide, HIO_COUNTOF(wide)) == 9 &&
	   hio_comp_ucstr(wide, wide_doc, 0) == 0,
		"valid IPv4 byte and wide formatting remain unchanged");
	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_LOG, 1, HIO_NULL);
	if (!hio) { bail_out("cannot open Hio"); return 1; }
	test_qx_slices(hio);
	test_sctp_metadata(hio);
	test_interface_descriptor_cleanup(hio);
	test_interface_lookup_errors(hio);
	test_interface_name_nuls(hio);
	test_reverse_lookup_errors(hio);
#if defined(AF_UNIX) && (HIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
	test_unix_path_limits(hio);
	test_unix_embedded_nuls(hio);
	test_unix_read_bounds(hio);
#else
	skip("Unix addresses are unavailable", 40);
#endif
	test_address_suffixes(hio, "127.0.0.1", HIO_AF_INET, 0, 0);
	test_address_suffixes(hio, "127.0.0.1:80", HIO_AF_INET, 80, 0);
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	test_scope_lookup_errors(hio);
	test_scope_embedded_nuls(hio);
	test_scope_output_lengths(hio);
	test_scope_descriptor_cleanup(hio);
	test_scope_id_limits(hio);
	test_excess_groups(hio);
	test_invalid_ipv6_syntax(hio);
	test_valid_ipv6(hio);
	test_address_suffixes(hio, "[::1]", HIO_AF_INET6, 0, 0);
	test_address_suffixes(hio, "[::1]:80", HIO_AF_INET6, 80, 0);
	test_address_suffixes(hio, "[fe80::1%7]", HIO_AF_INET6, 0, 7);
	test_address_suffixes(hio, "[fe80::1%7]:80", HIO_AF_INET6, 80, 7);
	test_address_suffixes(hio, "::1", HIO_AF_INET6, 0, 0);
	test_address_suffixes(hio, "fe80::1%7", HIO_AF_INET6, 0, 7);
#else
	skip("IPv6 is unavailable", 206);
#endif
	hio_close(hio);
	return exit_status();
}
