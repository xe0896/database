#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "btree.h"
#include "offsets.h"
#include "typedef.h"

NodeType get_node_type(void *node) {
    return *(uint8_t *)(node + NODE_TYPE_OFFSET);
}

void set_node_type(void *node, NodeType type) {
    *(uint8_t *)(node + NODE_TYPE_OFFSET) = type;
}

uint32_t *internal_node_num_cells(void *page) {
    return (uint32_t *)(page + NUM_CELLS_OFFSET);
}

uint32_t *leaf_node_num_cells(void *page) {
    // The number of cells located in the header
    return (uint32_t *)(page + NUM_CELLS_OFFSET);
}

void *internal_node_cell(void *node, uint32_t cell_num) {
    void *starting_cell = node + INTERNAL_NODE_HEADER_SIZE;
    return starting_cell + (cell_num * INTERNAL_NODE_CELL_SIZE);
}

void *internal_node_pointer(void *node, uint32_t cell_num) {
    void *cell = internal_node_cell(node, cell_num);
    return cell;
}

void initialise_leaf_node(void *node) {
    // Creating a leaf node would make the num cells being 0 (dereferencing)
    *leaf_node_num_cells(node) = 0;
    // Cast to uint8_t first to apply offset to a void pointer then dereference to then apply initialisation values
    set_next_leaf(node, 0);
    set_node_type(node, NODE_LEAF);
}

void *leaf_node_cell(void *page, uint32_t cell_num) {
    // Grab the number of cells for this given node/page
    // uint32_t number_of_cells = *leaf_node_num_cells(page);
    /* Not really necessary
    if(number_of_cells < cell_num) {
        printf("Provided cell number is beyond number of cells recorded.\n");
        exit(EXIT_FAILURE);
    }
    */

    // The starting cell would be the page + the INITIAL_CELL_OFFSET so we get to the first cell (to skip the header)
    void *starting_cell = page + INITIAL_CELL_OFFSET;
    // Given this starting cell we grab the cell_num and hop cell_num number of LEAF_NODE_CELL_SIZE's to get to the cell of the provided cell number
    return starting_cell + (cell_num * LEAF_NODE_CELL_SIZE);
}

void *internal_node_key(void *node, uint32_t cell_num) {
    void *cell = internal_node_cell(node, cell_num);
    return cell + INTERNAL_CELL_SIZE;
}

void *leaf_node_value(void *node, uint32_t cell_num) {
    // Given the node and the cell number we get the cell given the node/page and then increment the ID_SIZE to get the serialised row
    void *cell = leaf_node_cell(node, cell_num);
    return cell + ID_SIZE;
}

uint32_t *leaf_node_key(void *node, uint32_t cell_num) {
    void *cell = leaf_node_cell(node, cell_num);
    // Cast to a unsigned integer so that we know the bound (void* has no bound) to read the key
    return (uint32_t *)cell;
}

uint32_t get_unused_page_num(Pager *pager) {
    return pager->num_pages;
}

uint32_t next_page(Pager *pager) {
    return pager->num_pages++;
}

void set_node_root(void *page, bool status) {
    *(uint8_t *)(page + IS_ROOT_OFFSET) = status;
}

NodeType get_node_isroot(void *page) {
    return *(uint8_t *)(page + IS_ROOT_OFFSET);
}

void set_node_right_child(void *page, uint32_t page_num) {
    *(uint32_t *)(page + INTERNAL_NODE_RIGHT_NUM) = page_num;
}

void set_parent_pointer(void *page, uint32_t parent_num) {
    *(uint32_t *)(page + PARENT_POINTER_OFFSET) = parent_num;
}

uint32_t get_parent_pointer(void *page) {
    return *(uint32_t *)(page + PARENT_POINTER_OFFSET);
}

uint32_t *internal_right_child(void *page) {
    return (uint32_t *)(page + INTERNAL_NODE_RIGHT_NUM);
}

void set_next_leaf(void *page, uint32_t page_num) {
    *(uint32_t *)(page + NEXT_LEAF_OFFSET) = page_num;
}

void initialise_internal_node(void *page) {
    set_node_type(page, NODE_INTERNAL);
    *leaf_node_num_cells(page) = 1;
}

void insert_to_node(void *page, Cursor *cursor, Row row, int num_cells) {
    shift_insert(page, num_cells, cursor);
    *leaf_node_key(page, cursor->cell_num) = row.id;
    serialise(&row, leaf_node_value(page, cursor->cell_num));
    (*leaf_node_num_cells(page))++;
}

uint32_t get_next_leaf(void *page) {
    return *(uint32_t *)(page + NEXT_LEAF_OFFSET);
}