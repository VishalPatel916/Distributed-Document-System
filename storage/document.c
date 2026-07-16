#include "document.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

WordNode* create_word_node(const char* word_str) {
    WordNode* node = (WordNode*)malloc(sizeof(WordNode));
    node->word = strdup(word_str); 
    node->next = NULL;
    return node;
}

SentenceNode* create_sentence_node(char delim) {
    SentenceNode* node = (SentenceNode*)malloc(sizeof(SentenceNode));
    node->word_head = NULL;
    node->delimiter = delim;
    node->next = NULL;
    return node;
}

SentenceNode* parse_file_to_list(const char* file_path) {
    FILE* f = fopen(file_path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END); long file_size = ftell(f); rewind(f);
    char* buffer = (char*)malloc(file_size + 1);
    fread(buffer, 1, file_size, f);
    buffer[file_size] = '\0';
    fclose(f);

    SentenceNode* doc_head = NULL;
    SentenceNode* current_sent = NULL;
    WordNode* current_word = NULL;

    char* word_buffer = (char*)malloc(file_size + 1); // Buffer for the current word
    int word_idx = 0;
    
    // Start with a new sentence
    doc_head = create_sentence_node(' ');
    current_sent = doc_head;

    for (int i = 0; i < file_size; i++) {
        char c = buffer[i];

        if (c == '.' || c == '!' || c == '?') {
            // Found a sentence delimiter
            if (word_idx > 0) { // Save the last word
                word_buffer[word_idx] = '\0';
                WordNode* new_word = create_word_node(word_buffer);
                if (current_word == NULL) current_sent->word_head = new_word;
                else current_word->next = new_word;
                current_word = new_word;
                word_idx = 0;
            }
            
            // Create a new sentence
            current_sent->delimiter = c;
            SentenceNode* new_sent = create_sentence_node(' ');
            current_sent->next = new_sent;
            current_sent = new_sent;
            current_word = NULL;
        } 
        else if (isspace(c)) { // Found a word delimiter
            if (word_idx > 0) { // Save the last word
                word_buffer[word_idx] = '\0';
                WordNode* new_word = create_word_node(word_buffer);
                if (current_word == NULL) current_sent->word_head = new_word;
                else current_word->next = new_word;
                current_word = new_word;
                word_idx = 0;
            }
            // If it's a newline, it's also a sentence
            if (c == '\n') {
                current_sent->delimiter = '\n';
                SentenceNode* new_sent = create_sentence_node(' ');
                current_sent->next = new_sent;
                current_sent = new_sent;
                current_word = NULL;
            }
        } 
        else {
            // Just a normal character, add to word
            word_buffer[word_idx++] = c;
        }
    }

    // Save any trailing word
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

void free_document(SentenceNode* sent_head) {
    SentenceNode* current_sent = sent_head;
    while (current_sent != NULL) {
        WordNode* current_word = current_sent->word_head;
        while (current_word != NULL) {
            WordNode* next_word = current_word->next;
            free(current_word->word); free(current_word);
            current_word = next_word;
        }
        SentenceNode* next_sent = current_sent->next;
        free(current_sent); current_sent = next_sent;
    }
}

void flush_list_to_file(SentenceNode* sent_head, const char* file_path) {
    FILE* f = fopen(file_path, "w");
    if (!f) { log_event("  -> ERROR: Could not open file for flushing: %s", file_path); return; }

    SentenceNode* current_sent = sent_head;
    while (current_sent != NULL) {
        WordNode* current_word = current_sent->word_head;
        while (current_word != NULL) {
            fprintf(f, "%s", current_word->word);
            if (current_word->next != NULL) {
                fprintf(f, " "); // Add space between words
            }
            current_word = current_word->next;
        }
        
        if(current_sent->delimiter == '\n') {
            fprintf(f, "\n");
        } else if (current_sent->delimiter != ' ' || current_sent->next != NULL) {
            // Add delimiter if it's not a space, OR if it's a space
            // but not the very last sentence.
            fprintf(f, "%c ", current_sent->delimiter);
        }
        
        current_sent = current_sent->next;
    }
    fclose(f);
}

void handle_write_update_list(SentenceNode* doc_head, int sent_num, int word_idx, char* content) {
    // 1. Find the target sentence
    SentenceNode* target_sent = doc_head;
    for (int i = 0; i < sent_num && target_sent != NULL; i++) {
        target_sent = target_sent->next;
    }
    if (target_sent == NULL) {
        log_event("  -> ERROR: Sentence number %d out of bounds.", sent_num);
        return;
    }

    // 2. Find the insertion point (the node *before* word_idx)
    WordNode* insertion_point_prev = NULL;
    WordNode* insertion_point_next = target_sent->word_head;
    for (int i = 0; i < word_idx && insertion_point_next != NULL; i++) {
        insertion_point_prev = insertion_point_next;
        insertion_point_next = insertion_point_next->next;
    }

    // 3. Tokenize the new content by spaces
    char* content_copy = strdup(content); // Use a copy so strtok doesn't destroy original
    char* content_context = NULL;
    char* content_word = strtok_r(content_copy, " \t\r", &content_context);
    
    while(content_word != NULL) {
        // 4. Check *this word* for a delimiter
        char* delim_ptr = strpbrk(content_word, ".!?");
        
        if (delim_ptr != NULL) {
            // --- This word contains a delimiter! ---
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
            // --- Simple word, no delimiter ---
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

int count_words_in_string(const char* s) {
    if (!s) return 0;
    char* copy = strdup(s);
    char* ctx = NULL;
    char* tok = strtok_r(copy, " \t\r\n", &ctx);
    int cnt = 0;
    while (tok) {
        // Only count if it's not entirely delimiters
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

int apply_session_edits_to_sentence(ActiveDoc* doc, SentenceNode* target, WriteSession* session) {
    if (!doc || !target || !session) return -1;

    // Save the original sentence delimiter - we'll need it if no new delimiters are added
    char original_delimiter = target->delimiter;

    // 1. Build initial word list for the target sentence
    int orig_count = 0;
    WordNode* w = target->word_head;
    while (w) { orig_count++; w = w->next; }

    // Build dynamic array of strings
    int arr_capacity = orig_count + 256 + session->edit_count * 10;
    char** words = (char**)malloc(sizeof(char*) * (arr_capacity + 1));
    int idx = 0;
    w = target->word_head;
    while (w) { words[idx++] = strdup(w->word); w = w->next; }

    // 2. Apply each edit op in sequence, tracking cumulative insertions
    // Each edit's word_index refers to the position in the current array state
    for (int i = 0; i < session->edit_count; i++) {
        int insert_at = session->edit_ops[i].word_index;
        
        // Validate insertion point
        if (insert_at < 0 || insert_at > idx) {
            log_event("  -> ERROR: Edit %d has invalid word_index %d (array size %d)", i, insert_at, idx);
            for (int j = 0; j < idx; j++) free(words[j]);
            free(words);
            return -1;
        }
        
        // Tokenize content into words (split on whitespace, preserve delimiters IN words)
        char* copy = strdup(session->edit_ops[i].content);
        char* ctx = NULL;
        char* tok = strtok_r(copy, " \t\r\n", &ctx);
        
        while (tok) {
            // Ensure capacity
            if (idx + 1 > arr_capacity) {
                arr_capacity *= 2;
                words = (char**)realloc(words, sizeof(char*) * (arr_capacity + 1));
            }
            
            // Shift tail right to make room at insert_at
            for (int s = idx; s > insert_at; s--) {
                words[s] = words[s-1];
            }
            
            // Insert new word token
            words[insert_at] = strdup(tok);
            insert_at++;  // next token from same edit goes right after
            idx++;        // array grew
            
            tok = strtok_r(NULL, " \t\r\n", &ctx);
        }
        free(copy);
    }

    // 3. Now build new sentence nodes by splitting at delimiters
    // Delimiters can appear inside word strings (e.g., "hello." or "world!")
    // Key: We accumulate words into current sentence until we hit a delimiter
    // Each sentence starts with original_delimiter, overridden if a delimiter is found in content
    // Multiple consecutive delimiters (e.g., "cd...") create multiple empty sentences
    SentenceNode* first_new = NULL;
    SentenceNode* last_new = NULL;
    int cur = 0;
    
    while (cur < idx) {
        // Use original_delimiter as default for all sentences
        SentenceNode* snode = create_sentence_node(original_delimiter);
        snode->word_head = NULL;
        WordNode* last_word = NULL;
        
        while (cur < idx) {
            char* word_str = words[cur];
            
            // Check if this word contains a delimiter
            char* delim_pos = strpbrk(word_str, ".!?\n");
            
            if (delim_pos) {
                // Word contains delimiter - this ends the current sentence
                char delim_char = *delim_pos;
                
                // Part before delimiter becomes a word (if non-empty)
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
                
                // Override with the delimiter found in this word
                snode->delimiter = delim_char;
                
                // Check if there are MORE delimiters after this one (e.g., "cd..." has 3 dots)
                // We need to create empty sentences for each additional delimiter
                char* remaining = delim_pos + 1; // Start after first delimiter
                while (*remaining != '\0') {
                    if (strchr(".!?\n", *remaining)) {
                        // Another delimiter found - finish current sentence and create empty one
                        if (!first_new) first_new = snode;
                        else last_new->next = snode;
                        last_new = snode;
                        
                        // Create empty sentence with this delimiter
                        snode = create_sentence_node(*remaining);
                        snode->word_head = NULL;
                        last_word = NULL;
                        remaining++;
                    } else {
                        // Not a delimiter - this shouldn't happen with "cd..." but handle it
                        break;
                    }
                }
                
                cur++;
                break; // Move to next word (next sentence will be created in outer loop)
                
            } else {
                // Regular word, no delimiter - add to current sentence
                WordNode* wn = create_word_node(word_str);
                if (!snode->word_head) snode->word_head = wn;
                else last_word->next = wn;
                last_word = wn;
                cur++;
            }
        }
        
        // Attach sentence to list
        if (!first_new) first_new = snode;
        else last_new->next = snode;
        last_new = snode;
    }

    // 4. Replace target sentence in doc with new sentences
    SentenceNode* prev = NULL;
    SentenceNode* t = doc->doc_head;
    while (t && t != target) { prev = t; t = t->next; }
    
    if (!t) {
        log_event("  -> ERROR: Target sentence not found in document");
        for (int i = 0; i < idx; i++) free(words[i]);
        free(words);
        return -1;
    }

    // Link: prev -> first_new -> ... -> last_new -> target->next
    if (prev) prev->next = first_new;
    else doc->doc_head = first_new;
    
    if (last_new) last_new->next = target->next;

    // Free only the original target sentence (not its next chain)
    free_sentence_node(target);

    // Free word array
    for (int i = 0; i < idx; i++) free(words[i]);
    free(words);
    
    return 0;
}

ActiveDoc* find_or_load_active_doc(char* filename) {
    int doc_idx = find_active_doc(filename);
    if (doc_idx != -1) {
        log_event("  -> File '%s' is already active in memory.", filename);
        return &active_documents[doc_idx];
    }
    int new_slot = find_empty_active_doc_slot();
    if (new_slot == -1) { log_event("  -> ERROR: Active document list is full!"); return NULL; }
    
    ActiveDoc* doc = &active_documents[new_slot];
    log_event("  -> Loading file '%s' into active doc slot %d.", filename, new_slot);
    
    sprintf(doc->original_path, "%s/%s", g_storage_path, filename);
    sprintf(doc->backup_path, "%s/%s.bak", g_storage_path, filename);
    
    doc->doc_head = parse_file_to_list(doc->original_path);
    if (doc->doc_head == NULL) { log_event("  -> ERROR: Failed to parse file '%s' into list.", filename); return NULL; }

    doc->active = 1;
    strncpy(doc->filename, filename, MAX_FILENAME);
    doc->num_users_editing = 0;
    
    return doc;
}

void release_active_doc(ActiveDoc* doc) {
    doc->num_users_editing--;
    log_event("  -> User stopped editing '%s'. Active users: %d", doc->filename, doc->num_users_editing);
    if (doc->num_users_editing <= 0) {
        log_event("  -> No users left. Freeing document '%s' from memory.", doc->filename);
        free_document(doc->doc_head);
        doc->active = 0;
    }
}

