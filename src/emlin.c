//  Copyright (c) 2014 Jakub Filipowicz <jakubf@gmail.com>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <emelf.h>

#include "emlin.h"
#include "dh.h"

char *output_file;
int otype = O_EMELF;
int cpu = EMELF_CPU_MERA400;

struct emlin_object *objects;
struct emlin_object *entry;
int addr_top;

struct dh_table *names;

// -----------------------------------------------------------------------
static int add_libdir(char *dir)
{

	return 0;
}

// -----------------------------------------------------------------------
static void usage()
{
	printf("Usage: emlin [options] -o output input [input ...]\n");
	printf("Where options are one or more of:\n");
	printf("   -O <otype> : set output type: raw, emelf (defaults to raw)\n");
	printf("   -L <dir>   : search for libraries in <dir>\n");
    printf("   -v         : print version and exit\n");
	printf("   -h         : print help and exit\n");
}

// -----------------------------------------------------------------------
static int parse_args(int argc, char **argv)
{
	int option;
	struct emlin_object *obj;

	while ((option = getopt(argc, argv,"o:O:L:vh")) != -1) {
		switch (option) {
			case 'o':
				output_file = strdup(optarg);
				break;
			case 'O':
				if (!strcasecmp(optarg, "raw")) {
					otype = O_RAW;
				} else if (!strcasecmp(optarg, "emelf")) {
					otype = O_EMELF;
				} else {
					printf("Unknown output type: '%s'.\n", optarg);
					return -1;
				}
				break;
			case 'L':
				add_libdir(optarg);
				break;
			case 'v':
				printf("EMLIN v%s - linker for MERA 400 EMELF objects\n", EMLIN_VERSION);
				exit(0);
				break;
			case 'h':
				usage();
				exit(0);
				break;
			default:
				return -1;
		}
	}

	if (!output_file) {
		output_file = strdup("a.out");
	}

	while (optind < argc) {
		if (!strcmp(argv[optind], output_file)) {
			printf("Input file '%s' is also listed as an output file.\n", argv[optind]);
			return -1;
		}

		// add object to list
		obj = malloc(sizeof(struct emlin_object));
		obj->filename = strdup(argv[optind]);
		obj->e = NULL;
		obj->offset = -1;
		obj->entry = -1;
		obj->next = objects;
		objects = obj;

		optind++;
	}

	return 0;
}

// -----------------------------------------------------------------------
static int load_names(struct emlin_object *obj)
{
	struct emelf_symbol *sym;
	int symcount;
	char *sym_name;
	struct emlin_object *sym_obj;

	// copy global symbol names and assign objects
	sym = obj->e->symbol;
	symcount = obj->e->symbol_count;
	while (sym && (symcount > 0)) {
		sym_name = obj->e->symbol_names + sym->offset;
		if (sym->flags & EMELF_SYM_GLOBAL) {
			sym_obj = dh_get(names, sym_name);
			if (sym_obj) {
				printf("%s: Symbol '%s' already defined in object '%s'\n", obj->filename, sym_name, sym_obj->filename);
				return -1;
			}
			printf("%s: adding global name: %s\n", obj->filename, sym_name);
			dh_add(names, sym_name, obj);
		}
		sym++;
		symcount--;
	}

	return 0;
}

// -----------------------------------------------------------------------
static int load_objects()
{
	struct emlin_object *obj = objects;
	FILE *f;

	while (obj) {
		f = fopen(obj->filename, "r");
		if (!f) {
			printf("Cannot open file '%s' for reading.\n", obj->filename);
			return -1;
		}

		obj->e = emelf_load(f);
		fclose(f);
		if (!obj->e) {
			printf("Cannot load object file: %s\n", obj->filename);
			return -1;
		}

		if (emelf_has_entry(obj->e)) {
			if (entry) {
				printf("%s: entry point already defined in: %s\n", obj->filename, entry->filename);
				return -1;
			}
			entry = obj;
		}

		if (load_names(obj)) {
			return -1;
		}

		if (obj->e->eh.cpu == EMELF_CPU_MX16) {
			cpu = EMELF_CPU_MX16;
		}

		obj = obj->next;
	}

	return 0;
}

// -----------------------------------------------------------------------
static int link(struct emelf *e, struct emlin_object *obj)
{
	int res;
	struct emelf_reloc *reloc;
	int relcount;
	int sign;
	struct emlin_object *sym_obj;
	char *sym_name;
	struct emelf_symbol *sym;

	printf("%s: linking\n", obj->filename);

	// copy image
	res = emelf_image_append(e, obj->e->image, obj->e->image_pos);
	if (res != EMELF_E_OK)  {
		printf("%s: cannot append image.\n", obj->filename);
		return -1;
	}
	obj->offset = addr_top;
	addr_top += obj->e->image_pos;

	// scan relocs
	reloc = obj->e->reloc;
	relcount = obj->e->reloc_count;
	while (reloc && (relcount > 0)) {

		// @start reloc
		if (reloc->flags & EMELF_RELOC_BASE) {
			printf("%s: reloc @ %i @start: %i\n", obj->filename, reloc->addr + obj->offset, obj->offset);
			e->image[reloc->addr + obj->offset] += obj->offset;
		}

		// symbol reloc
		if (reloc->flags & EMELF_RELOC_SYM) {
			// handle negative symbols
			if (reloc->flags & EMELF_RELOC_SYM_NEG) {
				sign = -1;
			} else {
				sign = 1;
			}

			// find object that defines symbol
			sym_name = obj->e->symbol_names + reloc->sym_idx;
			printf("%s: references: %s\n", obj->filename, sym_name);
			sym_obj = dh_get(names, sym_name);
			if (!sym_obj) {
				printf("%s: symbol '%s' not defined.\n", obj->filename, sym_name);
				return -1;
			}

			printf("%s: '%s' found in: %s\n", obj->filename, sym_name, sym_obj->filename);
			// link the object referenced by symbol
			if (sym_obj->offset < 0) {
				if (link(e, sym_obj)) {
					return -1;
				}
			}

			// get the symbol
			sym = emelf_symbol_get(sym_obj->e, sym_name);
			if (!sym) {
				printf("%s: cannot get symbol '%s'.\n", sym_obj->filename, sym_name);
				return -1;
			}

			printf("%s: reloc @ %i by sym '%s': %i\n", obj->filename, reloc->addr + obj->offset, sym_name, sym->value);
			e->image[reloc->addr + obj->offset] += sign * sym->value;
			if (sym->flags & EMELF_SYM_RELATIVE) {
				printf("%s: reloc @ %i by sym '%s' @start: %i\n", obj->filename, reloc->addr + obj->offset, sym_name, sym_obj->offset);
				e->image[reloc->addr + obj->offset] += sign * sym_obj->offset;
			}
		}
		reloc++;
		relcount--;
	}

	return 0;
}

// -----------------------------------------------------------------------
int main(int argc, char **argv)
{
	int ret = 1;
	int res;
	struct emelf *e = NULL;
	FILE *f;

	res = parse_args(argc, argv);
	if (res) {
		goto cleanup;
	}

	if (!objects) {
		printf("No input files.\n");
		goto cleanup;
	}

	names = dh_create(64000);

	if (load_objects()) {
		goto cleanup;
	}

	if (!entry) {
		printf("No program entry point defined.\n");
		goto cleanup;
	}

	e = emelf_create(EMELF_EXEC, cpu);

	if (link(e, entry)) {
		goto cleanup;
	}

	res = emelf_entry_set(e, entry->e->eh.entry);
	if (res != EMELF_E_OK) {
		printf("Failed to set program entry point.\n");
		goto cleanup;
	}

	f = fopen(output_file, "w");
	if (!f) {
		printf("Cannot open output file '%s'.\n", output_file);
		goto cleanup;
	}

	if (otype == O_EMELF) {
		res = emelf_write(e, f);
	} else {
		int pos = e->image_pos;
		while (pos >= 0) {
			e->image[pos] = htons(e->image[pos]);
			pos--;
		}
		res = fwrite(e->image, sizeof(uint16_t), e->image_pos, f);
		if (res <= 0) {
			res = EMELF_E_FWRITE;
		} else {
			res = EMELF_E_OK;
		}
	}

	if (res != EMELF_E_OK) {
		fclose(f);
		printf("Cannot write output file '%s'.\n", output_file);
		goto cleanup;
	}

	fclose(f);

	ret = 0;

cleanup:
	emelf_destroy(e);
	dh_destroy(names);

	return ret;
}

// vim: tabstop=4 autoindent