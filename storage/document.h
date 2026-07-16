#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "storage_globals.h"
#include <string.h>

// --- Linked List Document Model ---
typedef struct WordNode {
    char* word;
    struct WordNode* next;
} WordNode;

typedef struct SentenceNode {
    WordNode* word_head;
    char delimiter;
    struct SentenceNode* next;
} SentenceNode;

// --- Active Document ---
typedef struct {
    int active;
    char filename[MAX_FILENAME];
    SentenceNode* doc_head;
    int num_users_editing;
    char original_path[768];
    char backup_path[768];
} ActiveDoc;
extern ActiveDoc active_documents[MAX_FILES_IN_SYSTEM];

// --- Edit Operation ---
typedef struct {
    int word_index;
    char content[256];
} EditOperation;

// --- Write Session ---
typedef struct {
    int active;
    int doc_index;
    SentenceNode* sentence_ptr;
    int original_word_count;
    EditOperation* edit_ops;
    int edit_count;
    int edit_capacity;
    int virtual_word_count;
} WriteSession;
extern WriteSession write_sessions[MAX_CONNECTIONS];

WordNode* create_word_node(const char* word_str);
SentenceNode* create_sentence_node(char delim);
void free_document(SentenceNode* sent_head);
void free_sentence_node(SentenceNode* sent);
SentenceNode* parse_file_to_list(const char* file_path);
void flush_list_to_file(SentenceNode* sent_head, const char* file_path);
void handle_write_update_list(SentenceNode* doc_head, int sent_num, int word_idx, char* content);

int find_empty_active_doc_slot();
int find_active_doc(char* filename);
ActiveDoc* find_or_load_active_doc(char* filename);
void release_active_doc(ActiveDoc* doc);
int count_words_in_string(const char* s);
int apply_session_edits_to_sentence(ActiveDoc* doc, SentenceNode* target, WriteSession* session);
int apply_session_edits_to_sentence_public(ActiveDoc* doc, SentenceNode* target, WriteSession* session);

#endif // DOCUMENT_H
