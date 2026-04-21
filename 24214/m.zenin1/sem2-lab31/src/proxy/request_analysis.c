#include <stdlib.h>
#include "proxy/request_analysis.h"

request_analyzis_result_t try_analyze_req_line(request_analysis_data_t *data) {
    return INCOMPLETE;
}

request_analyzis_result_t try_analyze_header(request_analysis_data_t *data) {
    return INCOMPLETE;
}

request_analyzis_result_t try_analyze(request_analysis_data_t *data) {
    switch (data->state) {
        case READING_REQUEST_LINE:
            return try_analyze_req_line(data);
        case READING_HEADERS:
            return try_analyze_header(data);
    }
}

void request_analysis_data_t_destruct(request_analysis_data_t *data) {
    if (data->uri != NULL) {
        free(data->uri);
    }
    vector_char_t_destruct(&data->data);
}
