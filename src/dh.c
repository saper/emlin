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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "dh.h"
#include "emlin.h"

// -----------------------------------------------------------------------
struct dh_table * dh_create(int size)
{
	struct dh_table *dh = malloc(sizeof(struct dh_table));
	if (!dh) {
		return NULL;
	}

	dh->slots = calloc(size, sizeof(struct dh_elem));
	if (!dh->slots) {
		free(dh);
		return NULL;
	}
	dh->size = size;

	return dh;
}

// -----------------------------------------------------------------------
unsigned dh_hash(struct dh_table *dh, char *str)
{
	unsigned v = 0;
	char *c = str;

	while (c && *c) {
		v = *c + (v<<5) - v;
		c++;
	}

	return v % dh->size;
}

// -----------------------------------------------------------------------
struct emlin_object * dh_get(struct dh_table *dh, char *name)
{
	unsigned hash = dh_hash(dh, name);
	struct dh_elem *elem = dh->slots[hash];

	while (elem) {
		if (!strcmp(name, elem->name)) {
			return elem->o;
		}
		elem = elem->next;
	}

	return NULL;
}

// -----------------------------------------------------------------------
struct dh_elem * dh_add(struct dh_table *dh, char *name, struct emlin_object *o)
{
	unsigned hash = dh_hash(dh, name);
	struct dh_elem *elem = dh->slots[hash];

	while (elem) {
		if (!strcmp(name, elem->name)) {
			return NULL;
		}
		elem = elem->next;
	}

	struct dh_elem *new_elem = malloc(sizeof(struct dh_elem));
	new_elem->name = strdup(name);
	new_elem->o = o;
	new_elem->next = dh->slots[hash];
	dh->slots[hash] = new_elem;

	return elem;
}

// -----------------------------------------------------------------------
int dh_delete(struct dh_table *dh, char *name)
{
	unsigned hash = dh_hash(dh, name);
	struct dh_elem *prev = NULL;
	struct dh_elem *elem = dh->slots[hash];

    while (elem) {
        if (!strcmp(name, elem->name)) {
			if (prev) {
				prev->next = elem->next;
			} else {
				dh->slots[hash] = elem->next;
			}
			free(elem->name);
			free(elem);
            return 0;
        }
		prev = elem;
        elem = elem->next;
    }
	return -1;
}

// -----------------------------------------------------------------------
void dh_destroy(struct dh_table *dh)
{
	int i;
	struct dh_elem *elem;
	struct dh_elem *tmp;

	if (!dh) return;

	for(i=0 ; i<dh->size ; i++) {
		elem = dh->slots[i];
		while (elem) {
			tmp = elem->next;
			free(elem->name);
			free(elem);
			elem = tmp;
		}
	}

	free(dh->slots);
	free(dh);
}

// -----------------------------------------------------------------------
void dh_dump_stats(struct dh_table *dh)
{
	int i;
	int elem_total = 0, collisions = 0;

	struct dh_elem *elem;

	if (!dh) return;

	unsigned *elem_slot = calloc(dh->size, sizeof(int));

	for(i=0 ; i<dh->size ; i++) {
		elem = dh->slots[i];
		while (elem) {
			elem_total++;
			elem_slot[i]++;
			if (elem_slot[i] > 1) {
				collisions++;
			}
			elem = elem->next;
		}
	}

	printf("-----------------------------------\n");
	printf("      Slots: %d\n", dh->size);
	printf("   Elements: %d\n", elem_total);
	printf(" Collisions: %d\n", collisions);
	printf("   Collided: \n");
	for(i=0 ; i<dh->size ; i++) {
		if (elem_slot[i] > 1) {
			printf(" %10d: %d\n", i, elem_slot[i]-1);
		}
	}
}

// vim: tabstop=4 autoindent
