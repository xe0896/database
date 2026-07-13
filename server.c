#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>

#include "protocol.h"
#include "typedef.h"
#include "offsets.h"

uint32_t get_next_leaf(void *page);
void set_next_leaf(void *page, uint32_t page_num);

void pager_flush(Pager *pager, uint32_t page_num) {
    // Sets the offset from the page to the provided page number (PAGE_SIZE to get it in bytes)
    lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    // Providing the page stored in the cache, write this into the provided file (nbytes: PAGE_SIZE)
    write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);
}

void db_close(Table *table) {
    Pager *pager = table->pager;
    // Get the num pages in the pager
    uint32_t num_pages = pager->num_pages;
    for (int i = 0; i < num_pages; i++) {
        // Given page pointer may be null
        void *ptr = pager->pages[i];
        // If it is null then we skip, nothing to copy into file
        if (!ptr)
            continue;
        // Save this given page number i
        pager_flush(pager, i);
        free(ptr);
    }

    int file_close = close(table->pager->file_descriptor);
    if (file_close == -1) {
        perror("close");
        exit(EXIT_FAILURE);
    }
    free(table->pager);
    free(table);
}

void *get_page(Pager *pager, uint32_t page_num) {
    // The page number being greater then the number of pages implies that there is not enough space as one node takes a page
    if (page_num >= TABLE_MAX_PAGES) {
        printf("Tried to fetch page out of bounds\n");
        exit(EXIT_FAILURE);
    }
    // May exist already, given the page number ask the pager for its page
    void *page = pager->pages[page_num];

    // If it exists then we can return it
    if (page != NULL) {
        return page;
    }

    // Given that it doesn't exist then it either doesn't exist at all or is in the file
    void *new_page = malloc(PAGE_SIZE);

    // uint32_t num_pages = pager->file_length / PAGE_SIZE;
    // if(pager->file_length % PAGE_SIZE) num_pages++; // partial page

    // In the file, the requested page number and it not being in the pager but still being within the number of pages means it is in the file
    if (page_num < pager->num_pages) {
        // Set the offset to be the page of the given page number (including header but we don't care we just want the whole page including the header)
        lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
        // off_t file_length = lseek(file_descriptor, 0, SEEK_END);
        // Read the page, would provide the bytes read into the buffer new_page
        ssize_t page_file = read(pager->file_descriptor, new_page, PAGE_SIZE);
        if (page_file == -1) {
            perror("read");
            exit(EXIT_FAILURE);
        }
    }

    // Could either be an empty page from previous malloc or could be a page read from the file
    pager->pages[page_num] = new_page;

    // Given the page_num being greater or equal to the number of pages recorded, we must update so we can flush accurately since
    // the flusher uses num_pages stored in pager as we add more pages
    if (page_num >= pager->num_pages) {
        // +1 due to indexing, if we think of the initial state where we have no pages, if we add a new page so lives in index 0
        // the number of pages is 1 not 0 so we can't do pager->num_pages = page_num (0)
        pager->num_pages = page_num + 1;
    }

    return new_page;
}

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

Cursor *table_start(Table *table) {
    Cursor *cursor = malloc(sizeof(Cursor));

    cursor->table = table;
    // The start of the table would start at cell number
    cursor->cell_num = 0;
    // Root page number is not 0, changes when we split
    void *node = get_page(table->pager, table->root_page_num);
    uint32_t page_num = table->root_page_num;

    while (get_node_type(node) == NODE_INTERNAL) {
        page_num = *(uint32_t *)internal_node_pointer(node, 0); // Left most pointer, we want to go down to the left
        node = get_page(table->pager, page_num);
    }

    cursor->page_num = page_num;
    uint32_t num_cells = *leaf_node_num_cells(node);

    // Number of cells being 0 mean that we are at the end since there are no cells yet, only in an empty table
    cursor->end_of_table = (num_cells == 0) ? true : false;
    return cursor;
}

Cursor *table_end(Table *table) {
    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    // End of the table we assign the cursor to the end of it
    cursor->page_num = table->root_page_num;

    // Given the root number we can get the node/page
    void *page = get_page(table->pager, table->root_page_num);
    // Given the node we can get the cell number by getting the num_cells in the header
    cursor->cell_num = *leaf_node_num_cells(page);
    // End of the table implies that this must be true
    cursor->end_of_table = true;

    return cursor;
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

void *cursor_value(Cursor *cursor) {
    // The cursor's currently position would be stored in its members, page_num and cell_num
    uint32_t page_num = cursor->page_num;
    uint32_t cell_num = cursor->cell_num;

    Pager *pager = cursor->table->pager;
    // Request the page stored for the cursor
    void *page = get_page(pager, page_num);

    printf("PAGE_NUM: %d\n", page_num);
    printf("CELL_NUMS: %d\n", cell_num);
    printf("NUM_CELLS: %d\n", *leaf_node_num_cells(page));
    // Return the cell for that cursor given the cell_num to get the offset from the page
    return leaf_node_value(page, cell_num);
}

void cursor_advance(Cursor *cursor) {
    // Given the cursor page number we get the page
    void *page = get_page(cursor->table->pager, cursor->page_num);

    // Request the number of cells for this page
    uint32_t num_cells = *leaf_node_num_cells(page);

    // Advancing a cursor would increment the cell_num (this is just advancing no read so increment straight away)
    cursor->cell_num++;

    // This cell number that we advanced to may be past the number of cells in this page, if so we must record it in the cursor
    if (cursor->cell_num >= num_cells) {
        // Given that we have reached teh end, then we are gonna go to the
        // nex leaf and this could be 0 meaning it is at the end or could be
        // ... claude
        uint32_t next = get_next_leaf(page);
        if (next == 0) {
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next;
            cursor->cell_num = 0;
        }
    }
}

PreparedResult prepare_insert(char *buffer, Statement *statement) {
    // Statement type is labelled as INSERT so execute command knows what to execute
    statement->type = STATEMENT_INSERT;
    // strtok saves its state so we read insert and pass input_buffer->buffer as the initial state and read until " " so reads "insert"
    strtok(buffer, " ");
    // Given that it saved it state we can pass NULL to make it start from the previous state, " " from the same idea above
    char *id_str = strtok(NULL, " ");
    char *username = strtok(NULL, " ");
    char *email = strtok(NULL, " ");

    // Given that this was insert since we checked beforehand, it must contain id, username and email
    if (id_str == NULL || username == NULL || email == NULL) {
        return PREPARED_SYNTAX_ERROR;
    }

    // Atoi makes the given string (char*) to the id
    int id = atoi(id_str);
    // May be negative so we must handle this error
    if (id < 0) {
        return PREPARED_NEGATIVE_ID;
    }

    // The given username and email may be to long, so prepare that error
    if (strlen(username) > USERNAME_SIZE || strlen(email) > EMAIL_SIZE) {
        return PREPARED_STRING_TO_LONG;
    }

    // Given that it passed the above, we can assign the row_to_insert for the given statement to hold the values it wants to insert
    statement->row_to_insert.id = id;
    // The destination is the 1st parameter, and given the string for the 2nd parameter copies the values into the destination
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARED_SUCCESS;
}

PreparedResult prepare_select(char *buffer, Statement *statement) {
    // Select is just print the whole thing so we just notify that the execute function knows that this statement is a select statement
    statement->type = STATEMENT_SELECT;

    return PREPARED_SUCCESS;
}

PreparedResult prepared_statement(char *buffer, Statement *statement) {
    // Given the buffer (input from the user), we prepare the appopriate commands
    if (strncmp(buffer, "insert", 6) == 0) {
        return prepare_insert(buffer, statement);
    } else if (strncmp(buffer, "select", 6) == 0) {
        return prepare_select(buffer, statement);
    }

    return PREPARED_UNRECOGNISED;
}

void print_row(Row *row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

// Given raw bytes, apply to struct
void deserialise(Row *destination, void *source) {
    // destination is the struct, memcpy(dst,src,size)
    memset(destination, 0, sizeof(Row)); // Set all to null terminators, so when we add our members it will have a null terminator at the end
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

// Given struct, apply to raw bytes
void serialise(Row *source, void *destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void shift_insert(void *page, uint32_t num_cells, Cursor *cursor) {
    // A case where this would just append would be when cursor->cell_num = L and num_cells is L so this won't execute
    // and given that cell_num points to the next available slot, the append would be applied using serialise(.., num_cells)
    // rather then making a new slot which is what the below does
    if (cursor->cell_num < num_cells) {
        // cursor->cell_num is wrong as it points to the end, not implemented yet
        for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
            // cursor->cell_num would be the position we want to leave empty for the cell to be inserted to
            // so we start at the end and copy the cell at page i - 1 to page i, which
            // overwrites and leaves a duplicate at the cursor->cell_num that can be overwritten

            // memcpy(dst, src, size)
            memcpy(leaf_node_cell(page, i), leaf_node_cell(page, i - 1), LEAF_NODE_CELL_SIZE);
        }
    }
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

void print_page(Pager *pager, void *current_page) {
    do {
        uint32_t num_cells = *leaf_node_num_cells(current_page);
        for (uint32_t i = 0; i < num_cells; i++) {
            void *value = leaf_node_value(current_page, i);
            uint32_t key = *leaf_node_key(current_page, i);
            Row row;
            deserialise(&row, value);
            printf("Key: %u, Row: %u %s %s\n", key, row.id, row.username, row.email);
        }

        uint32_t next = get_next_leaf(current_page);
        if (next == 0)
            break;
        current_page = get_page(pager, next);
    } while (1);
}

ExecuteResult split_insert(Cursor *cursor, Row row, void *page, uint32_t num_cells, uint32_t idx, uint32_t parent_num) {
    if (get_node_isroot(page) == true && get_node_type(page) == NODE_LEAF) {
        uint32_t id = row.id;
        Pager *pager = cursor->table->pager;
        // The node is full so we must split, we create a new page(s)
        uint32_t right_num = pager->num_pages;
        void *right_page = get_page(pager, right_num);
        initialise_leaf_node(right_page);

        uint32_t left_num = pager->num_pages;
        void *left_page = get_page(pager, left_num);
        initialise_leaf_node(left_page);

        uint32_t left_bound = (CELLS_PER_PAGE / 2) + 1;
        uint32_t right_bound = (CELLS_PER_PAGE / 2);

        if (CELLS_PER_PAGE % 2 == 0) {
            left_bound = (CELLS_PER_PAGE / 2);
        }

        // The given page of the full node (page) copies its upper half cells to the right child as the right child
        // needs to store the greater half of the elements to enforce binary search

        // If CELLS_PER_PAGE is odd like 6 then left should have 3 and right should have 3, so it should be an even distribution
        // so i < CELLS_PER_PAGE / 2 for left and i < CELLS_PER_PAGE / 2 for the right, for an odd case we add an extra to left (Random choice)
        // so i < (CELLS_PER_PAGE / 2) + 1 for the left loop

        for (uint32_t i = 0; i < right_bound; i++) { // CELLS_PER_PAGE = 13, (CELLS_PER_PAGE/2) = 6
            // The i will be for the location of the right child and the offset would be (CELL_PER_PAGE/2) for the
            // given page
            printf("RIGHT, i: %d, to: %d\n", i, i + (CELLS_PER_PAGE / 2));
            memcpy(leaf_node_cell(right_page, i), leaf_node_cell(page, i + left_bound), LEAF_NODE_CELL_SIZE);
        }

        for (uint32_t i = 0; i < left_bound; i++) {
            printf("LEFT, i: %d, to: %d\n", i, i);
            memcpy(leaf_node_cell(left_page, i), leaf_node_cell(page, i), LEAF_NODE_CELL_SIZE);
        }

        *leaf_node_num_cells(right_page) = right_bound;
        *leaf_node_num_cells(left_page) = left_bound;

        printf("This was called: %d\n", right_num);

        set_next_leaf(left_page, right_num);
        set_next_leaf(right_page, 0);

        // print_page(right_page);
        print_page(pager, left_page);
        printf("idx: %d\n", idx);

        // The idx provided by the location it should be by the prior binary search can determine whether or not it should be
        // given to the left or right leaf
        if (idx > left_bound) {
            // 7-12
            // Create space to insert if it is greater then CELLS_PER_PAGE/2 (right leaf)
            cursor->cell_num = idx - left_bound;
            insert_to_node(right_page, cursor, row, right_bound);
        } else {
            // 0-6
            cursor->cell_num = idx;
            insert_to_node(left_page, cursor, row, left_bound);
        }

        set_parent_pointer(right_page, parent_num);
        set_parent_pointer(left_page, parent_num);

        free(cursor);
        initialise_internal_node(page);
        // set_node_right_child(page, )
        uint32_t key = *(leaf_node_key(page, left_bound)); // key = max(left)
        printf("Key: %d\n", key);
        set_node_right_child(page, right_num);

        *(uint32_t *)internal_node_pointer(page, 0) = left_num;
        *(uint32_t *)internal_node_key(page, 0) = key;
        printf("reached\n");
    } else if (*leaf_node_num_cells(page) < KEY_VALUE_PER_PAGE && get_node_type(page) == NODE_INTERNAL) {
        // Internal node has space to add a key and pointer to the new splitted page
        /*
        uint32_t new_page_num = pager->num_pages;
        void* new_page = get_page(pager, new_page_num);
        */

        printf("Potentially reached\n");
    } else {
        printf("Unstable\n");
    }

    return EXECUTE_SUCCESS;
}

void *search_internals(Pager *pager, void *node, uint32_t *page_num, uint32_t id) {
    void *current_node = node;
    while (get_node_type(current_node) == NODE_INTERNAL) {
        uint32_t L = 0;
        uint32_t R = *internal_node_num_cells(current_node);

        while (L != R) {
            uint32_t midpoint = (L + R) / 2;
            uint32_t key = *(uint32_t *)internal_node_key(current_node, midpoint);
            // [1,5,8,10,12,14,16,20,21]
            // Given key: 13
            // idx=5 would be its spot since between 12 and 14 max(left) means that
            // the page 12 the maximum page is 12 so cannot contain 13, but 14 can contain it
            // we are not finding per se but looking for the correction insertion

            if (key < id) {
                L = midpoint + 1;
            } else {
                R = midpoint;
            }
        }

        if (L < *internal_node_num_cells(current_node)) {
            // We need to cast and dereference since page_num is still a local
            // the thing it is pointing to is not local
            *page_num = *(uint32_t *)internal_node_pointer(current_node, L);
            printf("Within: %d\n", *page_num);
            current_node = get_page(pager, *page_num);
        } else {
            *page_num = *(uint32_t *)internal_right_child(current_node);
            printf("Right: %d\n", *page_num);
            current_node = get_page(pager, *page_num);
        }
    }

    printf("Final page_num: %d\n", *page_num);

    return current_node;
}

ExecuteResult execute_insert(Statement *statement, Table *table) {
    // printf("Number of Rows: %d\n", table->num_rows);
    //  Since a node takes a whole page the amount of pages in the table (including the file as we calculate that when the db is opened,
    //  if it exceeds the maximum amount of pages the table would allow, return an error
    if (TABLE_MAX_PAGES <= table->pager->num_pages) {
        return EXECUTE_TABLE_FULL;
    }

    // The cursor can locate the cell number
    uint32_t id = statement->row_to_insert.id;

    // Returns the start of the page at the end
    void *page = get_page(table->pager, table->root_page_num);
    uint32_t parent_num = table->root_page_num;

    uint32_t page_num = 100;

    if (get_node_type(page) == NODE_INTERNAL) {
        page = search_internals(table->pager, page, &page_num, id);

        printf("Current page number: %d, Parent page number: %d\n", page_num, get_parent_pointer(page));
        /*
        NodeType type = get_node_type(page);

        char *str = (type == NODE_LEAF) ? "NODE_LEAF" : "NODE_INTERNAL";

        printf("Type is: %s\n", str);
        */
    }

    uint32_t num_cells = *leaf_node_num_cells(page);

    uint32_t L = 0;
    uint32_t R = num_cells;
    // The reason for the weird binary search is that we do not want
    // to do midpoint - 1 as uint_32t that becomes negative would underflow and become
    // very large, so we set it to the midpoint that was tested instead rather then skip it
    // we want this binary search to not only find a duplicate but also find an index
    // where we can insert this key into
    while (L != R) {
        uint32_t midpoint = (L + R) / 2;
        uint32_t key = *leaf_node_key(page, midpoint);
        if (key == id) {
            return EXECUTE_DUPLICATE_KEY;
        } else if (key < id) {
            L = midpoint + 1;
        } else {
            R = midpoint;
        }
    }

    // printf("idx: %d\n", L);

    // For the append case if the given id was the greatest of them all then
    // it wouldn't of triggered R = midpoint so then an L == R would imply that
    // we should just append, handled by shift_insert condition
    // Number of cells at the end since table->root_page_num from above
    Cursor *cursor = table_end(table);
    cursor->cell_num = L;

    if (num_cells >= CELLS_PER_PAGE) {
        printf("Max NUM CELLS REACHED BOIII.\n");
        // printf("num_cells: %d, CELLS_PER_PAGE: %d\n", num_cells, CELLS_PER_PAGE);

        return split_insert(cursor, statement->row_to_insert, page, num_cells, L, parent_num);

        //  return EXECUTE_TABLE_FULL;
        //  return split_insert(cursor, statement->row_to_insert);
    }

    shift_insert(page, num_cells, cursor);
    table->pager->dirty[table->root_page_num] = true;

    free(cursor);

    // leaf_node_key returns the pointer for the key for this given cell_num in this page, cast to uint32_t* to know how many bytes
    // then dereference to assign it to the id stored in the statement (assigning to the header in the cell)
    *leaf_node_key(page, L) = id;

    // Given the statement and the value of the leaf node for this cell number, copy the row into the bytes (insertion of serialised row)
    serialise(&(statement->row_to_insert), leaf_node_value(page, L));

    // Now we have add a new cell to this page, so increment
    (*leaf_node_num_cells(page))++;

    return EXECUTE_SUCCESS;
}

void print_tree(Pager *pager, uint32_t page_num, uint32_t depth) {
    void *node = get_page(pager, page_num);

    for (uint32_t i = 0; i < depth; i++)
        printf("  ");

    if (get_node_type(node) == NODE_LEAF) {
        uint32_t num_cells = *leaf_node_num_cells(node);
        printf("leaf (page %u, %u cells)\n", page_num, num_cells);
        for (uint32_t i = 0; i < num_cells; i++) {
            for (uint32_t j = 0; j < depth + 1; j++)
                printf("  ");
            printf("- key %u\n", *leaf_node_key(node, i));
        }
    } else {
        uint32_t num_keys = *internal_node_num_cells(node);
        printf("internal (page %u, %u keys)\n", page_num, num_keys);
        for (uint32_t i = 0; i < num_keys; i++) {
            print_tree(pager, *(uint32_t *)internal_node_pointer(node, i), depth + 1);
            for (uint32_t j = 0; j < depth + 1; j++)
                printf("  ");
            printf("key: %u\n", *(uint32_t *)internal_node_key(node, i));
        }
        print_tree(pager, *internal_right_child(node), depth + 1);
    }
}

ExecuteResult execute_select(Table *table) {
    // print_tree(table->pager, table->root_page_num, 0);

    Row row;
    Cursor *cursor = table_start(table);

    void *root_page = get_page(table->pager, table->root_page_num);

    printf("Right pointer: %d\n", *internal_right_child(root_page));

    while (!cursor->end_of_table) {
        deserialise(&row, cursor_value(cursor));
        // Advance
        cursor_advance(cursor);
        // Print the row
        print_row(&row);
    }

    free(cursor);

    /*
    Row row; // Ain't mallocin' since we would have to free
    Cursor *cursor = table_start(table);

    // When we are at the end of the table we stop printing
    while (!cursor->end_of_table) {
        // cursor_value returns the value it is at (serialised row) so we can use that as a parameter for
        // deserialise and use it as a source to populate the row
        deserialise(&row, cursor_value(cursor));
        // Advance
        cursor_advance(cursor);
        // Print the row
        print_row(&row);
    }

    free(cursor);
    */

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement *statement, Table *table) {
    switch (statement->type) {
    case (STATEMENT_SELECT):
        // printf("SELECT statement.\n");
        return execute_select(table);
    case (STATEMENT_INSERT):
        // printf("INSERT statement.\n");
        return execute_insert(statement, table);
    }
}

Pager *pager_open(const char *file_name) {
    // Create file_descriptor of the file, flags in 2nd param allow us to read and create a pre-existing file, 3rd param allow us to do the same
    // but for a new file since this would create a new file if the file_name doesn't exist
    int file_descriptor = open(file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

    if (file_descriptor == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(file_descriptor, 0, SEEK_END); // lseek would give the file length given SEEK_END
    if (file_length == -1) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }

    // We only save in chunks of pages, so the file_length must be cleanly divisible by PAGE_SIZE
    if (file_length % PAGE_SIZE != 0) {
        printf("Provided database file is not in chunks of pages.\n");
        exit(EXIT_FAILURE);
    }

    Pager *pager = malloc(sizeof(Pager));

    // A new pager starts off with an empty cache
    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    pager->file_descriptor = file_descriptor;
    pager->file_length = file_length;
    // The number of pages can be implied using the file length that should be in terms of PAGE_SIZES
    pager->num_pages = file_length / PAGE_SIZE;

    return pager;
}

Table *db_open(const char *file_name) {
    Table *table = malloc(sizeof(Table));
    Pager *pager = pager_open(file_name);
    table->pager = pager;
    table->root_page_num = 0;

    // Created database file, the last line of pager_open() would of made this not 0 if the file already has stuff in it
    if (pager->num_pages == 0) {
        // The root page wouldn't be a thing in a new file/empty file so we can assign it to 0, this would of eventually split
        void *root_page = get_page(pager, table->root_page_num);
        initialise_leaf_node(root_page);
        set_node_root(root_page, true);
    }

    return table;
}

void *handle_connection(void *args) {
    ThreadArgs *thread_args = (ThreadArgs *)args;
    int client_fd = thread_args->client_fd;
    Table *table = thread_args->table;

    free(thread_args);

    // Truly independent client being processed by the server after this

    while (1) {
        char *buf;
        uint32_t buf_len;
        RecvResult recv_result = recv_message(client_fd, &buf, &buf_len);
        // When the clients close its socket it would send a FIN which in TCP means that the connection has ended,
        // read() sees this and it won't be 4 bytes but a singular bit or something and then end the connection and call
        // this since it aint RECV_SUCCESS
        if (recv_result != RECV_SUCCESS) {
            printf("RECV_SUCCESS FAILURE\n");
            break;
        };
        Statement statement;

        PreparedResult prepared_result = prepared_statement(buf, &statement);

        char response[256];
        int len = 0;
        switch (prepared_result) {
        case (PREPARED_SUCCESS):
            break;
        case (PREPARED_UNRECOGNISED):
            len = snprintf(response, sizeof(response), "Unrecognised command '%s'.\n", buf);
            break;
        case (PREPARED_SYNTAX_ERROR):
            len = snprintf(response, sizeof(response), "Syntax error, could not parse statement.\n");
            break;
        case (PREPARED_NEGATIVE_ID):
            len = snprintf(response, sizeof(response), "Negative ID.\n");
            break;
        case (PREPARED_STRING_TO_LONG):
            len = snprintf(response, sizeof(response), "Strings provided are to long\n");
            break;
        }

        if (len != 0) {
            SendResult send_result = send_message(client_fd, response, len);
            if (send_result != SEND_SUCCESS) {
                printf("SEND_SUCCESS FAILURE1\n");
                close(client_fd);
                exit(EXIT_FAILURE);
            }
            continue;
        }

        ExecuteResult execute_result = execute_statement(&statement, table);
        switch (execute_result) {
        case (EXECUTE_SUCCESS):
            len = snprintf(response, sizeof(response), "Executed.\n");
            break;
        case (EXECUTE_TABLE_FULL):
            len = snprintf(response, sizeof(response), "Table is full.\n");
            break;
        case (EXECUTE_DUPLICATE_KEY):
            len = snprintf(response, sizeof(response), "Duplicate key provided.\n");
            break;
        }

        if (len != 0) {
            SendResult send_result = send_message(client_fd, response, len);
            if (send_result != SEND_SUCCESS) {
                printf("SEND_SUCCESS FAILURE2\n");
                close(client_fd);
                exit(EXIT_FAILURE);
            }
        }
    }
    close(client_fd); // When an error arises for the while loop above, then we must close that client file descriptor
    return NULL;
}

// A fork was used earlier but this won't make sense for a shared database as a fork would create a pager cache for each
// new user which could work but when multiple users are requesting the same page then a shared cache becomes ideal, shifting
// the idea from fork to threads
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Must provide a database name, for example: './server storage.db'\n");
        exit(EXIT_FAILURE);
    }

    const char *file_name = argv[1];
    Table *table = db_open(file_name);

    // hints is to provide the parameters of the connection
    // ptr is going to walk through the link list we get from res in getaddrinfo()
    struct addrinfo hints;

    struct addrinfo *ptr, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // Wildcard address, accept any connection so including 127.0.0.1 (server behaviour)
    hints.ai_protocol = IPPROTO_TCP;

    // NULL says that "I do not have an address to look up, I am the one being looked up"
    // &hinst is providing the parameters
    // &res would return us the start of the linked list of *potential* available addresses to bind with
    int addr_status = getaddrinfo(NULL, PORT, &hints, &res);
    if (addr_status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(addr_status));
        exit(EXIT_FAILURE);
    }

    int sock_fd;

    // Walks the link list that we got from getaddrinfo which has returned a bunch of potential addresses that may work
    // that satisfy the hints we provided earlier, availability depends on stuff mainly from the bind() call and whether or not
    // a certain address is available
    for (ptr = res; ptr != NULL; ptr = ptr->ai_next) {
        // Passes the stuff we provided in hints but may differ from connection to connection
        sock_fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

        // -1 implies that there was an error so this wasn't successful
        if (sock_fd == -1)
            continue;

        // Allow rebinding to a port still in TIME_WAIT from the previous run,
        // otherwise bind() fails with EADDRINUSE right after a Ctrl-C restart
        int yes = 1;
        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        // Potential winner so we pass the IP and port which are buried under
        // ai_addr, we also pass the length of the struct stored in ai_addrlen
        // so it knows how much it can safely read
        int bind_sock = bind(sock_fd, ptr->ai_addr, ptr->ai_addrlen);

        // A bind of 0 implies it was a success so we break
        if (bind_sock == 0) {
            break;
        } else if (bind_sock == -1) {
            perror("bind");
            close(sock_fd);
            continue;
        }

        close(sock_fd);
    }

    if (ptr == NULL) {
        fprintf(stderr, "bind: failed on every candidate\n");
        exit(EXIT_FAILURE);
    }

    // No longer needed
    freeaddrinfo(res);

    // Maximum of people allowed in the queue is backlog
    int backlog = 50;
    // Server would be listening
    int listen_status = listen(sock_fd, backlog);

    if (listen_status == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Parent process
    while (true) {
        // A generic struct that is large enough to hold either IPv4 or IPv6, accept states that it needs to write
        // the client's address and that is the 2nd param in accept
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);

        // sock_fd is just used to accepts clients, not used in any write or read
        int client_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            perror("accept");
            continue; // Don't exit if one bad accept
        }

        pthread_t tid;

        // The reason why we make a pointer instead of a stack struct is because it would be overwritten
        // as we loop around
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->client_fd = client_fd;
        args->table = table;

        // The thread is created and is told to work on handle_connection(), then the server loops around
        // and accepts any other new client/user through the blocked accept (waits for the next user)
        pthread_create(&tid, NULL, handle_connection, args);
    }
}