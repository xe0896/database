#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "typedef.h"
#include "offsets.h"

void read_input(InputBuffer *input_buffer) {
    // getline takes the buffer and the size of it and updates what is provided by the user to the buffer and updates length
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if(bytes_read <= 0) {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    } 

    input_buffer->input_length = bytes_read - 1; // Remove the newline from user pressing enter, .exit\n\0 -> .exit\n
    input_buffer->buffer[bytes_read - 1] = 0; // Replace \n with \0, to \n
}

void pager_flush(Pager* pager, uint32_t page_num) {
    // Sets the offset from the page to the provided page number (PAGE_SIZE to get it in bytes)
    lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    // Providing the page stored in the cache, write this into the provided file (nbytes: PAGE_SIZE)
    write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);
}

void free_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

void db_close(Table* table) {
    Pager* pager = table->pager;
    // Get the num pages in the pager
    uint32_t num_pages = pager->num_pages;
    for(int i = 0; i < num_pages; i++) {
        // Given page pointer may be null
        void* ptr = pager->pages[i];
        // If it is null then we skip, nothing to copy into file
        if(!ptr) continue;
        // Save this given page number i
        pager_flush(pager, i);
        free(ptr);
    }

    int file_close = close(table->pager->file_descriptor);
    if(file_close == -1) {
        perror("close");
        exit(EXIT_FAILURE);
    }
    free(table->pager);
    free(table);
}


void* get_page(Pager* pager, uint32_t page_num) {
    // The page number being greater then the number of pages implies that there is not enough space as one node takes a page
    if (page_num >= TABLE_MAX_PAGES) {
        printf("Tried to fetch page out of bounds\n");
        exit(EXIT_FAILURE);
    }
    // May exist already, given the page number ask the pager for its page
    void* page = pager->pages[page_num];

    // If it exists then we can return it
    if(page != NULL) {
        return page;
    }

    // Given that it doesn't exist then it either doesn't exist at all or is in the file
    void* new_page = malloc(PAGE_SIZE);

    //uint32_t num_pages = pager->file_length / PAGE_SIZE;
    //if(pager->file_length % PAGE_SIZE) num_pages++; // partial page

    // In the file, the requested page number and it not being in the pager but still being within the number of pages means it is in the file
    if(page_num < pager->num_pages) {
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
    if(page_num >= pager->num_pages) {
        // +1 due to indexing, if we think of the initial state where we have no pages, if we add a new page so lives in index 0
        // the number of pages is 1 not 0 so we can't do pager->num_pages = page_num (0) 
        pager->num_pages = page_num + 1; 
    }

    return new_page;
}

NodeType get_node_type(void* node) {
    return *(uint8_t*)(node + NODE_TYPE_OFFSET);
}

void set_node_type(void* node, NodeType type) {
    *(uint8_t*)(node + NODE_TYPE_OFFSET) = type;
}

uint32_t* leaf_node_num_cells(void* page) {
    // The number of cells located in the header
    return (uint32_t*)(page + NUM_CELLS_OFFSET);
}

Cursor* table_start(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));

    cursor->table = table;
    // The start of the table would start at cell number
    cursor->cell_num = 0;
    // Root page number is not 0, changes when we split
    cursor->page_num = table->root_page_num;

    // Get the page of the root node
    void* root_node = get_page(table->pager, table->root_page_num);
    // The number of cells in the root node
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    
    // Number of cells being 0 mean that we are at the end since there are no cells yet, only in an empty table
    cursor->end_of_table = (num_cells == 0) ? true : false;
    return cursor;
}

Cursor* table_end(Table* table) {
    Cursor * cursor = malloc(sizeof(Cursor));
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

void initialise_leaf_node(void* node) {
    // Creating a leaf node would make the num cells being 0 (dereferencing)
    *leaf_node_num_cells(node) = 0;
    // Cast to uint8_t first to apply offset to a void pointer then dereference to then apply initialisation values

    set_node_type(node, NODE_LEAF);
}

void* leaf_node_cell(void* page, uint32_t cell_num) {
    // Grab the number of cells for this given node/page
    //uint32_t number_of_cells = *leaf_node_num_cells(page);
    /* Not really necessary
    if(number_of_cells < cell_num) {
        printf("Provided cell number is beyond number of cells recorded.\n");
        exit(EXIT_FAILURE);
    }
    */   

    // The starting cell would be the page + the INITIAL_CELL_OFFSET so we get to the first cell (to skip the header)
    void* starting_cell = page + INITIAL_CELL_OFFSET;
    // Given this starting cell we grab the cell_num and hop cell_num number of LEAF_NODE_CELL_SIZE's to get to the cell of the provided cell number
    return starting_cell + (cell_num * LEAF_NODE_CELL_SIZE);
}

void* leaf_node_value(void* node, uint32_t cell_num) {
    // Given the node and the cell number we get the cell given the node/page and then increment the ID_SIZE to get the serialised row
    void* cell = leaf_node_cell(node, cell_num);
    return cell + ID_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
    void* cell = leaf_node_cell(node, cell_num);
    // Cast to a unsigned integer so that we know the bound (void* has no bound) to read the key
    return (uint32_t*) cell;
}

uint32_t get_unused_page_num(Pager* pager) {
    return pager->num_pages;
}

void* cursor_value(Cursor* cursor) {
    // The cursor's currently position would be stored in its members, page_num and cell_num
    uint32_t page_num = cursor->page_num;
    uint32_t cell_num = cursor->cell_num;

    Pager* pager = cursor->table->pager;
    // Request the page stored for the cursor
    void* page = get_page(pager, page_num);

    // Return the cell for that cursor given the cell_num to get the offset from the page
    return leaf_node_value(page, cell_num);
}

void cursor_advance(Cursor* cursor) {
    // Advancing a cursor would increment the cell_num (this is just advancing no read so increment straight away)
    cursor->cell_num++;
    // Given the cursor page number we get the page
    void* page = get_page(cursor->table->pager, cursor->page_num);

    // Request the number of cells for this page
    uint32_t num_cells = *leaf_node_num_cells(page);
    
    // This cell number that we advanced to may be past the number of cells in this page, if so we must record it in the cursor
    if(cursor->cell_num >= num_cells) {
        cursor->end_of_table = true;
    }
}

MetaCommandResult meta_command(InputBuffer* input_buffer, Table* table) {
    // Meta commands are in the form .x 
    if(strcmp(input_buffer->buffer, ".exit") == 0) {
        free_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_SUCCESS);
    } else {
        return META_COMMAND_UNRECOGNISED;
    }
}

PreparedResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
    // Statement type is labelled as INSERT so execute command knows what to execute
    statement->type = STATEMENT_INSERT;
    // strtok saves its state so we read insert and pass input_buffer->buffer as the initial state and read until " " so reads "insert"
    strtok(input_buffer->buffer, " ");
    // Given that it saved it state we can pass NULL to make it start from the previous state, " " from the same idea above
    char* id_str = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

    // Given that this was insert since we checked beforehand, it must contain id, username and email
    if(id_str == NULL || username == NULL || email == NULL) {
        return PREPARED_SYNTAX_ERROR;
    }

    // Atoi makes the given string (char*) to the id
    int id = atoi(id_str);
    // May be negative so we must handle this error
    if(id < 0) {
        return PREPARED_NEGATIVE_ID;
    }

    // The given username and email may be to long, so prepare that error
    if(strlen(username) > USERNAME_SIZE || strlen(email) > EMAIL_SIZE) {
        return PREPARED_STRING_TO_LONG;
    }

    // Given that it passed the above, we can assign the row_to_insert for the given statement to hold the values it wants to insert
    statement->row_to_insert.id = id;
    // The destination is the 1st parameter, and given the string for the 2nd parameter copies the values into the destination
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARED_SUCCESS;
}

PreparedResult prepare_select(InputBuffer* input_buffer, Statement* statement) {
    // Select is just print the whole thing so we just notify that the execute function knows that this statement is a select statement
    statement->type = STATEMENT_SELECT;

    return PREPARED_SUCCESS;
}

PreparedResult prepared_statement(InputBuffer* input_buffer, Statement* statement) {
    // Given the buffer (input from the user), we prepare the appopriate commands
    if(strncmp(input_buffer->buffer, "insert", 6) == 0) {
        return prepare_insert(input_buffer, statement);
    } else if (strncmp(input_buffer->buffer, "select", 6) == 0) {
        return prepare_select(input_buffer, statement);
    }

    return PREPARED_UNRECOGNISED;
}

InputBuffer* new_input_buffer() {
    // A new buffer would assume everything being empty
    InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}

void print_row(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

// Given raw bytes, apply to struct
void deserialise(Row* destination, void* source) {
    // destination is the struct, memcpy(dst,src,size)
    memset(destination, 0, sizeof(Row));// Set all to null terminators, so when we add our members it will have a null terminator at the end
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

void shift_insert(void* page, uint32_t num_cells, Cursor* cursor) {   
    // A case where this would just append would be when cursor->cell_num = L and num_cells is L so this won't execute
    // and given that cell_num points to the next available slot, the append would be applied using serialise(.., num_cells)
    // rather then making a new slot which is what the below does
    if(cursor->cell_num < num_cells) {
        // cursor->cell_num is wrong as it points to the end, not implemented yet
        for(uint32_t i = num_cells; i > cursor->cell_num; i--) {
            // cursor->cell_num would be the position we want to leave empty for the cell to be inserted to
            // so we start at the end and copy the cell at page i - 1 to page i, which 
            // overwrites and leaves a duplicate at the cursor->cell_num that can be overwritten

            // memcpy(dst, src, size)
            memcpy(leaf_node_cell(page, i), leaf_node_cell(page, i - 1), LEAF_NODE_CELL_SIZE);
        }
    }
}

void set_node_root(void* page, bool status) {
    *(uint8_t *)(page + IS_ROOT_OFFSET) = status;
}

void initialise_internal_node(void* page) {
    set_node_type(page, NODE_INTERNAL);    
    set_node_root(page, true);
    *leaf_node_num_cells(page) = 0;
}

ExecuteResult split_insert(Cursor* cursor, Row row, void* page, uint32_t num_cells, uint32_t idx) {
    uint32_t id = row.id;
    // The node is full so we must split, we create a new page for the (right leaf)
    void* right_child = malloc(PAGE_SIZE);
    void* left_child = malloc(PAGE_SIZE);
    // Initialise it
    initialise_leaf_node(right_child);
    initialise_leaf_node(left_child);
    
    // The given page of the full node (page) copies its upper half cells to the right child as the right child
    // needs to store the greater half of the elements to enforce binary search
    for(uint32_t i = 0; i < (CELLS_PER_PAGE/2); i++) { // CELLS_PER_PAGE = 13, (CELLS_PER_PAGE/2) = 6
        // The i will be for the location of the right child and the offset would be (CELL_PER_PAGE/2) for the 
        // given page
        printf("RIGHT, i: %d, to: %d\n", i, i + (CELLS_PER_PAGE/2) + 1);
        memcpy(leaf_node_cell(right_child, i), leaf_node_cell(page, i + (CELLS_PER_PAGE/2) + 1), LEAF_NODE_CELL_SIZE);
    }

    for(uint32_t i = 0; i < (CELLS_PER_PAGE/2) + 1; i++) {
        printf("LEFT, i: %d, to: %d\n", i, i);
        memcpy(leaf_node_cell(left_child, i), leaf_node_cell(page, i), LEAF_NODE_CELL_SIZE);
    }


    printf("idx: %d\n", idx);

    // The idx provided by the location it should be by the prior binary search can determine whether or not it should be
    // given to the left or right leaf
    if(idx > CELLS_PER_PAGE/2) {
        // Create space to insert if it is greater then CELLS_PER_PAGE/2 (right leaf)
        cursor->cell_num = idx - ((CELLS_PER_PAGE/2) + 1);
        shift_insert(right_child, CELLS_PER_PAGE/2, cursor);
    } else {
        cursor->cell_num = idx;
        shift_insert(left_child, (CELLS_PER_PAGE/2), cursor);
    }
    
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_insert(Statement* statement, Table *table) {
    //printf("Number of Rows: %d\n", table->num_rows);
    // Since a node takes a whole page the amount of pages in the table (including the file as we calculate that when the db is opened,
    // if it exceeds the maximum amount of pages the table would allow, return an error
    if(TABLE_MAX_PAGES <= table->pager->num_pages) {
        return EXECUTE_TABLE_FULL;
    }

    // Returns the start of the page at the end
    void *page = get_page(table->pager, table->root_page_num);
    uint32_t num_cells = *leaf_node_num_cells(page);

    // The cursor can locate the cell number
    int id = statement->row_to_insert.id;

    uint32_t L = 0;
    uint32_t R = num_cells;
    // The reason for the weird binary search is that we do not want
    // to do midpoint - 1 as uint_32t that becomes negative would underflow and become
    // very large, so we set it to the midpoint that was tested instead rather then skip it
    // we want this binary search to not only find a duplicate but also find an index
    // where we can insert this key into
    while(L != R) {
        uint32_t midpoint = (L + R)/2;
        uint32_t key = *leaf_node_key(page, midpoint);
        if(key == id) {
            return EXECUTE_DUPLICATE_KEY;
        } else if (key < id) {
            L = midpoint + 1;
        } else {
            R = midpoint;
        }  
    }

    printf("idx: %d\n", L);

    // For the append case if the given id was the greatest of them all then
    // it wouldn't of triggered R = midpoint so then an L == R would imply that 
    // we should just append, handled by shift_insert condition
    // Number of cells at the end since table->root_page_num from above
    Cursor* cursor = table_end(table);
    cursor->cell_num = L;

    if(num_cells >= CELLS_PER_PAGE) {
        printf("Max NUM CELLS REACHED BOIII.\n");
        printf("num_cells: %d, CELLS_PER_PAGE: %d\n", num_cells, CELLS_PER_PAGE);
        return split_insert(cursor, statement->row_to_insert, page, num_cells, L);
        //return EXECUTE_TABLE_FULL;
        //return split_insert(cursor, statement->row_to_insert);
    }

    shift_insert(page, num_cells, cursor);
    free(cursor);

    // leaf_node_key returns the pointer for the key for this given cell_num in this page, cast to uint32_t* to know how many bytes
    // then dereference to assign it to the id stored in the statement (assigning to the header in the cell)
    *(uint32_t*)leaf_node_key(page, L) = id;

    // Given the statement and the value of the leaf node for this cell number, copy the row into the bytes (insertion of serialised row)
    serialise(&(statement->row_to_insert), leaf_node_value(page, L));

    // Now we have add a new cell to this page, so increment
    (*leaf_node_num_cells(page))++;

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Table *table) {
    Row row; // Ain't mallocin' since we would have to free
    Cursor* cursor = table_start(table);

    // When we are at the end of the table we stop printing
    while(!cursor->end_of_table) {
        // cursor_value returns the value it is at (serialised row) so we can use that as a parameter for
        // deserialise and use it as a source to populate the row 
        deserialise(&row, cursor_value(cursor));
        // Advance
        cursor_advance(cursor);
        // Print the row
        print_row(&row);
    }
    
    free(cursor);

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement* statement, Table* table) {
    switch(statement->type) {
        case(STATEMENT_SELECT):
            //printf("SELECT statement.\n");
            return execute_select(table);
        case(STATEMENT_INSERT):
            //printf("INSERT statement.\n");
            return execute_insert(statement, table);
    }
}

void print_prompt() {
    printf("db > ");
}

Pager* pager_open(const char* file_name) {
    // Create file_descriptor of the file, flags in 2nd param allow us to read and create a pre-existing file, 3rd param allow us to do the same
    // but for a new file since this would create a new file if the file_name doesn't exist
    int file_descriptor = open(file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

    if(file_descriptor == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(file_descriptor, 0, SEEK_END); // lseek would give the file length given SEEK_END
    if(file_length == -1) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }

    // We only save in chunks of pages, so the file_length must be cleanly divisible by PAGE_SIZE
    if(file_length % PAGE_SIZE != 0) {
        printf("Provided database file is not in chunks of pages.\n");
        exit(EXIT_FAILURE);
    }

    Pager* pager = malloc(sizeof(Pager));

    // A new pager starts off with an empty cache
    for(int i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    pager->file_descriptor = file_descriptor;
    pager->file_length = file_length;
    // The number of pages can be implied using the file length that should be in terms of PAGE_SIZES
    pager->num_pages = file_length/PAGE_SIZE;
    
    return pager;
}

Table* db_open(const char* file_name) {
    Table* table = malloc(sizeof(Table));
    Pager* pager = pager_open(file_name);
    table->pager = pager;
    table->root_page_num = 0;

    // Created database file, the last line of pager_open() would of made this not 0 if the file already has stuff in it
    if(pager->num_pages == 0) {
        // The root page wouldn't be a thing in a new file/empty file so we can assign it to 0, this would of eventually split
        void *root_page = get_page(pager, 0);
        initialise_leaf_node(root_page);
    }

    return table;
}

int main(int argc, char* argv[]) {
    InputBuffer* input_buffer = new_input_buffer();

    if(argc < 2) {
        printf("Must provide a database name, for example: './main storage.db'\n");
        exit(EXIT_FAILURE);
    }

    const char* file_name = argv[1];
    Table* table = db_open(file_name);

    /*
    Row row;
    Row out;
    row.id = 5;
    (row.username, "David");
    strcpy(row.email, "xenonmmo@gmail.com");

    void* bytes = malloc(4096);
    serialise(&row, bytes);
    deserialise(&out, bytes);

    printf("ID: %d\n", out.id);
    printf("Username: %s\n", out.username);
    printf("Email: %s\n", out.email);
    */
    
    while(true) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer[0] == '.') {
            switch(meta_command(input_buffer, table)) {
                case(META_COMMAND_SUCCESS):
                    continue;
                case(META_COMMAND_UNRECOGNISED):
                    printf("Unrecognised command '%s'.\n", input_buffer->buffer);
                    continue;
            }
        }
        Statement statement;
        switch(prepared_statement(input_buffer, &statement)) {
            case(PREPARED_SUCCESS):
                break;
            case(PREPARED_UNRECOGNISED):
                printf("Unrecognised command '%s'.\n", input_buffer->buffer);
                continue;
            case(PREPARED_SYNTAX_ERROR):
                printf("Syntax error, could not parse statement.\n");
                continue;
            case(PREPARED_NEGATIVE_ID):
                printf("Negative ID.\n");
                continue;
            case(PREPARED_STRING_TO_LONG):
                printf("Strings provided are to long\n");
                continue;
        }
        switch(execute_statement(&statement, table)) {
            case(EXECUTE_SUCCESS):
                printf("Executed.\n");
                break;
            case(EXECUTE_TABLE_FULL):
                printf("Table is full.\n");
                break;
            case(EXECUTE_DUPLICATE_KEY):
                printf("Duplicate key provided.\n");
                break;
        }  
    }
}
