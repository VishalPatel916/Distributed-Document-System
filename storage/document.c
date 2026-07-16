// Implementation of in-memory document structures.

#include "document.h"
#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

extern void log_event(const char* format, ...);

// Allocate and initialize a new WordNode.
WordNode* create_word_node(const char* word_str) {
    WordNode* node = (WordNode*)malloc(sizeof(WordNode));
    node->word = strdup(word_str); 
    node->next = NULL;
    return node;
}

// Allocate and initialize a new SentenceNode.
SentenceNode* create_sentence_node(char delim) {
    SentenceNode* node = (SentenceNode*)malloc(sizeof(SentenceNode));
    node->word_head = NULL;
    node->delimiter = delim;
    node->next = NULL;
    return node;
}

// Parse file content into a linked list structure.
SentenceNode* parse_file_to_list(const char* file_path) {
    FILE* f = fopen(file_path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END); 
    long file_size = ftell(f); 
    rewind(f);
    
    char* buffer = (char*)malloc(file_size + 1);
    if (fread(buffer, 1, file_size, f) < 0) {
        perror("read file to list");
    }
    buffer[file_size] = '\0';
    fclose(f);

    SentenceNode* doc_head = NULL;
    SentenceNode* current_sent = NULL;
    WordNode* current_word = NULL;

    char* word_buffer = (char*)malloc(file_size + 1);
    int word_idx = 0;
    
    doc_head = create_sentence_node(' ');
    current_sent = doc_head;

    for (int i = 0; i < file_size; i++) {
        char c = buffer[i];

        if (c == '.' || c == '!' || c == '?') {
            if (word_idx > 0) {
                word_buffer[word_idx] = '\0';
                WordNode* new_word = create_word_node(word_buffer);
                if (current_word == NULL) current_sent->word_head = new_word;
                else current_word->next = new_word;
                current_word = new_word;
                word_idx = 0;
            }
            
            current_sent->delimiter = c;
            SentenceNode* new_sent = create_sentence_node(' ');
            current_sent->next = new_sent;
            current_sent = new_sent;
            current_word = NULL;
        } 
        else if (isspace(c)) {
            if (word_idx > 0) {
                word_buffer[word_idx] = '\0';
                WordNode* new_word = create_word_node(word_buffer);
                if (current_word == NULL) current_sent->word_head = new_word;
                else current_word->next = new_word;
                current_word = new_word;
                word_idx = 0;
            }
            if (c == '\n') {
                current_sent->delimiter = '\n';
                SentenceNode* new_sent = create_sentence_node(' ');
                current_sent->next = new_sent;
                current_sent = new_sent;
                current_word = NULL;
            }
        } 
        else {
            word_buffer[word_idx++] = c;
        }
    }

    if (word_idx > 0) {
        word_buffer[word_idx] = '\0';
        WordNode* new_word = create_word_node(word_buffer);
        if (current_word == NULL) current_sent->word_head = new_word;
        else current_word->next = new_word;
    }

    free(buffer);
    free(word_buffer);
    return doc_head;
}

// Free memory allocated for the document list.
void free_document(SentenceNode* sent_head) {
    SentenceNode* current_sent = sent_head;
    while (current_sent != NULL) {
        WordNode* current_word = current_sent->word_head;
        while (current_word != NULL) {
            WordNode* next_word = current_word->next;
            free(current_word->word); 
            free(current_word);
            current_word = next_word;
        }
        SentenceNode* next_sent = current_sent->next;
        free(current_sent); 
        current_sent = next_sent;
    }
}

// Write the document list back to local disk.
void flush_list_to_file(SentenceNode* sent_head, const char* file_path) {
    FILE* f = fopen(file_path, "w");
    if (!f) { 
        log_event("  -> ERROR: Could not open file for flushing: %s", file_path); 
        return; 
    }

    SentenceNode* current_sent = sent_head;
    while (current_sent != NULL) {
        WordNode* current_word = current_sent->word_head;
        while (current_word != NULL) {
            fprintf(f, "%s", current_word->word);
            if (current_word->next != NULL) {
                fprintf(f, " ");
            }
            current_word = current_word->next;
        }
        
        if (current_sent->delimiter == '\n') {
            fprintf(f, "\n");
        } else if (current_sent->delimiter != ' ' || current_sent->next != NULL) {
            fprintf(f, "%c ", current_sent->delimiter);
        }
        
        current_sent = current_sent->next;
    }
    fclose(f);
}

// Process write updates and rebuild sentences when delimiters are added.
void handle_write_update_list(SentenceNode* doc_head, int sent_num, int word_idx, char* content) {
    SentenceNode* target_sent = doc_head;
    for (int i = 0; i < sent_num && target_sent != NULL; i++) {
        target_sent = target_sent->next;
    }
    if (target_sent == NULL) {
        log_event("  -> ERROR: Sentence number %d out of bounds.", sent_num);
        return;
    }

    WordNode* insertion_point_prev = NULL;
    WordNode* insertion_point_next = target_sent->word_head;
    for (int i = 0; i < word_idx && insertion_point_next != NULL; i++) {
        insertion_point_prev = insertion_point_next;
        insertion_point_next = insertion_point_next->next;
    }

    char* content_copy = strdup(content);
    char* content_context = NULL;
    char* content_word = strtok_r(content_copy, " \t\r", &content_context);
    
    while (content_word != NULL) {
        char* delim_ptr = strpbrk(content_word, ".!?");
        
        if (delim_ptr != NULL) {
            char delim_char = *delim_ptr;
            *delim_ptr = '\0'; 
            
            if (strlen(content_word) > 0) {
                WordNode* new_word = create_word_node(content_word);
                if (insertion_point_prev == NULL) target_sent->word_head = new_word;
                else insertion_point_prev->next = new_word;
                new_word->next = NULL; 
                insertion_point_prev = new_word;
            }
            
            SentenceNode* new_sent = create_sentence_node(target_sent->delimiter); 
            target_sent->delimiter = delim_char;
            
            new_sent->next = target_sent->next;
            target_sent->next = new_sent;
            new_sent->word_head = insertion_point_next;
            
            content_word = delim_ptr + 1;
            if (strlen(content_word) > 0) {
                WordNode* final_word = create_word_node(content_word);
                final_word->next = new_sent->word_head; 
                new_sent->word_head = final_word;
                insertion_point_prev = final_word;
            } else {
                insertion_point_prev = NULL;
            }

            target_sent = new_sent;
            insertion_point_next = target_sent->word_head;

        } else {
            WordNode* new_word = create_word_node(content_word);
            if (insertion_point_prev == NULL) target_sent->word_head = new_word;
            else insertion_point_prev->next = new_word;
            new_word->next = insertion_point_next;
            insertion_point_prev = new_word;
        }
        
        content_word = strtok_r(NULL, " \t\r", &content_context);
    }
    free(content_copy);
}

// Count number of words in a string.
int count_words_in_string(const char* s) {
    if (!s) return 0;
    char* copy = strdup(s);
    char* ctx = NULL;
    char* tok = strtok_r(copy, " \t\r\n", &ctx);
    int cnt = 0;
    while (tok) {
        int has_non_delim = 0;
        for (int i = 0; tok[i] != '\0'; i++) {
            if (!strchr(".!?\n", tok[i])) {
                has_non_delim = 1;
                break;
            }
        }
        if (has_non_delim) {
            cnt++;
        }
        tok = strtok_r(NULL, " \t\r\n", &ctx);
    }
    free(copy);
    return cnt;
}

// Apply session edits to a sentence and adjust list boundaries.
int apply_session_edits_to_sentence(ActiveDoc* doc, SentenceNode* target, WriteSession* session) {
    if (!doc || !target || !session) return -1;

    char original_delimiter = target->delimiter;

    int orig_count = 0;
    WordNode* w = target->word_head;
    while (w) { 
        orig_count++; 
        w = w->next; 
    }

    int arr_capacity = orig_count + 256 + session->edit_count * 10;
    char** words = (char**)malloc(sizeof(char*) * (arr_capacity + 1));
    int idx = 0;
    w = target->word_head;
    while (w) { 
        words[idx++] = strdup(w->word); 
        w = w->next; 
    }

    for (int i = 0; i < session->edit_count; i++) {
        int insert_at = session->edit_ops[i].word_index;
        
        if (insert_at < 0 || insert_at > idx) {
            log_event("  -> ERROR: Edit %d has invalid word_index %d (array size %d)", i, insert_at, idx);
            for (int j = 0; j < idx; j++) free(words[j]);
            free(words);
            return -1;
        }
        
        char* copy = strdup(session->edit_ops[i].content);
        char* ctx = NULL;
        char* tok = strtok_r(copy, " \t\r\n", &ctx);
        
        while (tok) {
            if (idx + 1 > arr_capacity) {
                arr_capacity *= 2;
                words = (char**)realloc(words, sizeof(char*) * (arr_capacity + 1));
            }
            
            for (int s = idx; s > insert_at; s--) {
                words[s] = words[s-1];
            }
            
            words[insert_at] = strdup(tok);
            insert_at++;  
            idx++;        
            
            tok = strtok_r(NULL, " \t\r\n", &ctx);
        }
        free(copy);
    }

    SentenceNode* first_new = NULL;
    SentenceNode* last_new = NULL;
    int cur = 0;
    
    while (cur < idx) {
        SentenceNode* snode = create_sentence_node(original_delimiter);
        snode->word_head = NULL;
        WordNode* last_word = NULL;
        
        while (cur < idx) {
            char* word_str = words[cur];
            char* delim_pos = strpbrk(word_str, ".!?\n");
            
            if (delim_pos) {
                char delim_char = *delim_pos;
                int before_len = delim_pos - word_str;
                
                if (before_len > 0) {
                    char before_delim[MAX_WORD_CONTENT];
                    strncpy(before_delim, word_str, before_len);
                    before_delim[before_len] = '\0';
                    
                    WordNode* wn = create_word_node(before_delim);
                    if (!snode->word_head) snode->word_head = wn;
                    else last_word->next = wn;
                    last_word = wn;
                }
                
                snode->delimiter = delim_char;
                char* remaining = delim_pos + 1;
                
                while (*remaining != '\0') {
                    if (strchr(".!?\n", *remaining)) {
                        if (!first_new) first_new = snode;
                        else last_new->next = snode;
                        last_new = snode;
                        
                        snode = create_sentence_node(*remaining);
                        snode->word_head = NULL;
                        last_word = NULL;
                        remaining++;
                    } else {
                        break;
                    }
                }
                
                cur++;
                break; 
                
            } else {
                WordNode* wn = create_word_node(word_str);
                if (!snode->word_head) snode->word_head = wn;
                else last_word->next = wn;
                last_word = wn;
                cur++;
            }
        }
        
        if (!first_new) first_new = snode;
        else last_new->next = snode;
        last_new = snode;
    }

    SentenceNode* prev = NULL;
    SentenceNode* t = doc->doc_head;
    while (t && t != target) { 
        prev = t; 
        t = t->next; 
    }
    
    if (!t) {
        log_event("  -> ERROR: Target sentence not found in document");
        for (int i = 0; i < idx; i++) free(words[i]);
        free(words);
        return -1;
    }

    if (prev) prev->next = first_new;
    else doc->doc_head = first_new;
    
    if (last_new) last_new->next = target->next;

    free_sentence_node(target);

    for (int i = 0; i < idx; i++) free(words[i]);
    free(words);
    
    return 0;
}

// Find document or load it from disk into memory.
ActiveDoc* find_or_load_active_doc(char* filename) {
    int doc_idx = find_active_doc(filename);
    if (doc_idx != -1) {
        log_event("  -> File '%s' is already active in memory.", filename);
        return &active_documents[doc_idx];
    }
    int new_slot = find_empty_active_doc_slot();
    if (new_slot == -1) { 
        log_event("  -> ERROR: Active document list is full!"); 
        return NULL; 
    }
    
    ActiveDoc* doc = &active_documents[new_slot];
    log_event("  -> Loading file '%s' into active doc slot %d.", filename, new_slot);
    
    sprintf(doc->original_path, "%s/%s", g_storage_path, filename);
    sprintf(doc->backup_path, "%s/%s.bak", g_storage_path, filename);
    
    doc->doc_head = parse_file_to_list(doc->original_path);
    if (doc->doc_head == NULL) { 
        log_event("  -> ERROR: Failed to parse file '%s' into list.", filename); 
        return NULL; 
    }

    doc->active = 1;
    strncpy(doc->filename, filename, MAX_FILENAME);
    doc->num_users_editing = 0;
    
    return doc;
}

// Release document from active memory if no users are editing.
void release_active_doc(ActiveDoc* doc) {
    doc->num_users_editing--;
    log_event("  -> User stopped editing '%s'. Active users: %d", doc->filename, doc->num_users_editing);
    if (doc->num_users_editing <= 0) {
        log_event("  -> No users left. Freeing document '%s' from memory.", doc->filename);
        free_document(doc->doc_head);
        doc->active = 0;
    }
}
