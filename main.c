#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "typedef.h"

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = COLUMN_USERNAME_SIZE;
const uint32_t EMAIL_SIZE = COLUMN_EMAIL_SIZE;

const uint8_t NODE_TYPE_SIZE = HEADER_NODE_TYPE_SIZE;
const uint8_t IS_ROOT_SIZE = HEADER_IS_ROOT_SIZE;
const uint32_t PARENT_POINTER_SIZE = HEADER_PARENT_POINTER_SIZE;
const uint32_t NUM_CELLS_SIZE = HEADER_NUM_CELLS_SIZE;
const uint32_t HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE + NUM_CELLS_SIZE;

const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = ID_SIZE + ROW_SIZE;

const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_OFFSET + NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint32_t NUM_CELLS_OFFSET = PARENT_POINTER_OFFSET + PARENT_POINTER_SIZE;
const uint32_t INITIAL_CELL_OFFSET = NUM_CELLS_OFFSET + NUM_CELLS_SIZE;

const uint32_t ROWS_PER_PAGE = PAGE_SIZE/ROW_SIZE;
const uint32_t AVAILABLE_CELLS_SPACE = PAGE_SIZE - HEADER_SIZE;
const uint32_t CELLS_PER_PAGE = AVAILABLE_CELLS_SPACE/LEAF_NODE_CELL_SIZE;
const uint32_t TABLE_MAX_SIZE = TABLE_MAX_PAGES * ROWS_PER_PAGE;

void free_input_buffer(InputBuffer* input_buffer);
ExecuteResult execute_statement(Statement* statement, Table* table);

void read_input(InputBuffer *input_buffer) {
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if(bytes_read <= 0) {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    } 

    input_buffer->input_length = bytes_read - 1; // Remove the newline from user pressing enter, .exit\n\0 -> .exit\n
    input_buffer->buffer[bytes_read - 1] = 0; // Replace \n with \0, to \n
}

void pager_flush(Pager* pager, uint32_t page_num) {
    lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);
}

void db_close(Table* table) {
    Pager* pager = table->pager;
    uint32_t num_pages = pager->num_pages;
    for(int i = 0; i < num_pages; i++) {
        void* ptr = pager->pages[i];
        if(!ptr) continue;
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

Cursor* table_start(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));

    cursor->table = table;
    cursor->cell_num = 0;
    cursor->page_num = table->root_page_num;

    void* root_node = get_page(table->pager, table->root_page_num);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    
    cursor->end_of_table = (num_cells == 0) ? true : false;
    return cursor;
}

Cursor* table_end(Table* table) {
    Cursor * cursor = malloc(sizeof(Cursor));

    cursor->table = table;

    cursor->page_num = table->root_page_num;

    void *page = get_page(table->pager, table->root_page_num);
    cursor->cell_num = *leaf_node_num_cells(page);
   
    cursor->end_of_table = true;

    return cursor;
}

void initialise_leaf_node(void* node) {
    *leaf_node_num_cells(node) = 0;
}

void* leaf_node_value(void* node, uint32_t cell_num) {
    void* cell = leaf_node_cell(node, cell_num);
    return cell + ID_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
    void* cell = leaf_node_cell(node, cell_num);
    // Cast to a unsigned integer so that we know the bound (void* has no bound)
    // to read the key
    return (uint32_t*) cell;
}

void* leaf_node_cell(void* page, uint32_t cell_num) {
    uint32_t number_of_cells = leaf_node_num_cells(page);
    if(number_of_cells < cell_num) {
        printf("Provided cell number is beyond number of cells recorded.\n");
        exit(EXIT_FAILURE);
    }    

    void* starting_cell = page + INITIAL_CELL_OFFSET;
    return starting_cell + (cell_num * LEAF_NODE_CELL_SIZE);
}

uint32_t* leaf_node_num_cells(void* page) {
    return (uint32_t *)(page + NUM_CELLS_OFFSET);
}

void* get_page(Pager* pager, uint32_t page_num) {
    if (page_num >= TABLE_MAX_PAGES) {
        printf("Tried to fetch page out of bounds\n");
        exit(EXIT_FAILURE);
    }
    void* page = pager->pages[page_num];

    if(page != NULL) {
        return page;
    }

    void* new_page = malloc(PAGE_SIZE);

    uint32_t num_pages = pager->file_length / PAGE_SIZE;
    //if(pager->file_length % PAGE_SIZE) num_pages++; // partial page

    // In the file
    if(page_num <= num_pages) {
        lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
        // off_t file_length = lseek(file_descriptor, 0, SEEK_END);
        ssize_t page_file = read(pager->file_descriptor, new_page, PAGE_SIZE);
        if (page_file == -1) {
            perror("read");
            exit(EXIT_FAILURE);
        }
    }

    pager->pages[page_num] = new_page;

    if(page_num >= pager->num_pages) {
        pager->num_pages = page_num + 1; // +1 due to indexing
    }

    return new_page;
}

void* cursor_value(Cursor* cursor) {
    uint32_t page_num = cursor->page_num;
    uint32_t cell_num = cursor->cell_num;
    Pager* pager = cursor->table->pager;
    void* page = get_page(pager, page_num);

    return leaf_node_value(page, cell_num);
}

void cursor_advance(Cursor* cursor) {
    cursor->cell_num++;
    void* page = get_page(cursor->table->pager, cursor->page_num);

    uint32_t num_cells = *leaf_node_num_cells(page);
    
    if(cursor->cell_num >= num_cells) {
        cursor->end_of_table = true;
    }
}

MetaCommandResult meta_command(InputBuffer* input_buffer, Table* table) {
    if(strcmp(input_buffer->buffer, ".exit") == 0) {
        free_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_SUCCESS);
    } else {
        return META_COMMAND_UNRECOGNISED;
    }
}

PreparedResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
    statement->type = STATEMENT_INSERT;
    char* insert = strtok(input_buffer->buffer, " ");
    char* id_str = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

    if(id_str == NULL || username == NULL || email == NULL) {
        return PREPARED_SYNTAX_ERROR;
    }

    int id = atoi(id_str);
    if(id < 0) {
        return PREPARED_NEGATIVE_ID;
    }

    if(strlen(username) > USERNAME_SIZE || strlen(email) > EMAIL_SIZE) {
        return PREPARED_STRING_TO_LONG;
    }

    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARED_SUCCESS;
}

PreparedResult prepare_select(InputBuffer* input_buffer, Statement* statement) {
    statement->type = STATEMENT_SELECT;

    return PREPARED_SUCCESS;
}

PreparedResult prepared_statement(InputBuffer* input_buffer, Statement* statement) {
    if(strncmp(input_buffer->buffer, "insert", 6) == 0) {
        return prepare_insert(input_buffer, statement);
    } else if (strncmp(input_buffer->buffer, "select", 6) == 0) {
        return prepare_select(input_buffer, statement);
    }

    return PREPARED_UNRECOGNISED;
}

InputBuffer* new_input_buffer() {
    InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}

void free_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

void print_row(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

// Given raw bytes, apply to struct
void deserialise(Row* destination, void* source) {
    // destination is the struct
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

ExecuteResult execute_insert(Statement* statement, Table *table) {
    //printf("Number of Rows: %d\n", table->num_rows);
    if(TABLE_MAX_PAGES <= table->pager->num_pages) {
        return EXECUTE_TABLE_FULL;
    }

    // We call row slot as a specific row is shared amongst pages and this page may live in the file
    // or in the current cache

    Cursor* cursor = table_end(table);
    void *page = cursor_value(cursor);
    free(cursor);
    serialise(&(statement->row_to_insert), page);
    /*
    Row row;
    deserialise(&row, page);
    printf("ID: %d, username: %s, email: %s\n", row.id, row.username, row.email);
    */
    table->num_rows++;

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Table *table) {
    Row row;
    Cursor* cursor = table_start(table);

    while(!cursor->end_of_table) {
        deserialise(&row, cursor_value(cursor));
        cursor_advance(cursor);
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
    int file_descriptor = open(file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

    if(file_descriptor == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(file_descriptor, 0, SEEK_END);
    if(file_length == -1) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }

    Pager* pager = malloc(sizeof(Pager));

    for(int i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    pager->file_descriptor = file_descriptor;
    pager->file_length = file_length;
    pager->num_pages = file_length/PAGE_SIZE;
    
    return pager;
}

Table* db_open(const char* file_name) {
    Table* table = malloc(sizeof(Table));
    Pager* pager = pager_open(file_name);
    table->pager = pager;

    /*
    for(int i = 0; i < TABLE_MAX_PAGES; i++) {
        table->pager->pages[i] = NULL;
    }
    */

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

        }  
    }
}
