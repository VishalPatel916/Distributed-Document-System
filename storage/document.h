// Header for document state and linked list representations.

#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "storage_globals.h"
#include "protocol.h"
#include <string.h>

// Represents a single word in a sentence.
typedef struct WordNode {
    char* word;             // < The null-terminated string of the word.
    struct WordNode* next;  // < Pointer to the next word in the sentence.
} WordNode;

// Represents a sentence, containing a list of words.
typedef struct SentenceNode {
    WordNode* word_head;          // < Head of the linked list of words.
    char delimiter;               // < The punctuation or newline ending the sentence.
    struct SentenceNode* next;    // < Pointer to the next sentence in the document.
} SentenceNode;

// Represents a file currently loaded into memory for editing.
typedef struct {
    int active;                     // < 1 if slot is used, 0 if free.
    char filename[MAX_FILENAME];    // < The name of the file.
    SentenceNode* doc_head;         // < Head of the sentence linked list.
    int num_users_editing;          // < Count of active lock holders.
    char original_path[768];        // < Physical path to the main file.
    char backup_path[768];          // < Physical path to the backup file.
} ActiveDoc;
extern ActiveDoc active_documents[MAX_FILES_IN_SYSTEM];

// Represents a single modification requested by a client.
typedef struct {
    int word_index;                 // < Position to insert the word (relative to original).
    char content[256];              // < The word content to insert.
} EditOperation;

// Tracks the state of an active client write connection.
typedef struct {
    int active;                     // < 1 if session is active, 0 otherwise.
    int doc_index;                  // < Index in the active_documents array.
    SentenceNode* sentence_ptr;     // < Pointer to the locked sentence.
    int original_word_count;        // < Words in sentence at time of lock acquisition.
    EditOperation* edit_ops;        // < Dynamic array of accumulated edits.
    int edit_count;                 // < Number of current edits.
    int edit_capacity;              // < Allocated capacity for edit_ops.
    int virtual_word_count;         // < Simulated word count for indexing validation.
} WriteSession;
extern WriteSession write_sessions[MAX_CONNECTIONS];

// Memory allocation and deallocation
WordNode* create_word_node(const char* word_str);
SentenceNode* create_sentence_node(char delim);
void free_document(SentenceNode* sent_head);
void free_sentence_node(SentenceNode* sent);

// File I/O parsing and flushing
SentenceNode* parse_file_to_list(const char* file_path);
void flush_list_to_file(SentenceNode* sent_head, const char* file_path);

// Modifying the list (legacy/simple method)
void handle_write_update_list(SentenceNode* doc_head, int sent_num, int word_idx, char* content);

// Active Document management
int find_empty_active_doc_slot();
int find_active_doc(char* filename);
ActiveDoc* find_or_load_active_doc(char* filename);
void release_active_doc(ActiveDoc* doc);

// Collaborative Editing logic
int count_words_in_string(const char* s);
int apply_session_edits_to_sentence(ActiveDoc* doc, SentenceNode* target, WriteSession* session);
int apply_session_edits_to_sentence_public(ActiveDoc* doc, SentenceNode* target, WriteSession* session);

#endif // DOCUMENT_H
