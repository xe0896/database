#ifndef BTREE_H
#define BTREE_H

#include <stdint.h>
#include <stdbool.h>

#include "typedef.h"

void shift_insert(void *page, uint32_t num_cells, Cursor *cursor);
void serialise(Row *source, void *destination);
NodeType get_node_type(void *node);
void set_node_type(void *node, NodeType type);
uint32_t *internal_node_num_cells(void *page);
uint32_t *leaf_node_num_cells(void *page);
void *internal_node_cell(void *node, uint32_t cell_num);
void *internal_node_pointer(void *node, uint32_t cell_num);
void initialise_leaf_node(void *node);
void *leaf_node_cell(void *page, uint32_t cell_num);
void *internal_node_key(void *node, uint32_t cell_num);
void *leaf_node_value(void *node, uint32_t cell_num);
uint32_t *leaf_node_key(void *node, uint32_t cell_num);
uint32_t get_unused_page_num(Pager *pager);
uint32_t next_page(Pager *pager);
void set_node_root(void *page, bool status);
NodeType get_node_isroot(void *page);
void set_node_right_child(void *page, uint32_t page_num);
void set_parent_pointer(void *page, uint32_t parent_num);
uint32_t get_parent_pointer(void *page);
uint32_t *internal_right_child(void *page);
void set_next_leaf(void *page, uint32_t page_num);
void initialise_internal_node(void *page);
void insert_to_node(void *page, Cursor *cursor, Row row, int num_cells);
uint32_t get_next_leaf(void *page);

#endif