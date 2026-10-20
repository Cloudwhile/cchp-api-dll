#include "cchp.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s; %s\n", __LINE__, #x, cchp_last_error()); return 1; } } while (0)
int main(void) {
    cchp_client client;
    CHECK(cchp_client_create("{") == NULL);
    CHECK(strstr(cchp_last_error(), "\"code\":-1") != NULL);
    client = cchp_client_create("{\"auth\":{\"type\":\"bearer\",\"token\":\"test-only\"}}");
    CHECK(client != NULL);
    CHECK(strcmp(cchp_last_error(), "{}") == 0);
    cchp_string_free(NULL);
    cchp_client_destroy(client);
    CHECK(strcmp(cchp_last_error(), "{}") == 0);
    cchp_client_destroy(client);
    CHECK(strstr(cchp_last_error(), "Invalid client") != NULL);
    return 0;
}
