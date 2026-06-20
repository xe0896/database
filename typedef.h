#ifndef INITIAL
#define INITIAL

#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define TABLE_MAX_PAGES 100
#define PAGE_SIZE 4096

#define HEADER_NODE_TYPE_SIZE sizeof(uint8_t)
#define HEADER_IS_ROOT_SIZE sizeof(uint8_t)
#define HEADER_PARENT_POINTER_SIZE sizeof(uint32_t)
#define HEADER_NUM_CELLS_SIZE sizeof(uint32_t)

typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNISED
} MetaCommandResult;

typedef enum {
    PREPARED_SUCCESS,
    PREPARED_UNRECOGNISED,
    PREPARED_SYNTAX_ERROR,
    PREPARED_NEGATIVE_ID,
    PREPARED_STRING_TO_LONG
} PreparedResult;

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;   

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef enum {
    NODE_INTERNAL,
    NODE_LEAF
} NodeType;

typedef struct {
    uint32_t id; 
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

typedef struct {
    StatementType type;
    Row row_to_insert;
} Statement;

typedef struct {
    int file_descriptor;
    uint32_t file_length;
    uint32_t num_pages;
    void* pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
    uint32_t root_page_num;
    Pager* pager;
} Table;

typedef struct {
    Table* table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;
} Cursor;

#endif