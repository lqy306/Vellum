/*
 * test-ai-completion-parsing.c
 * 直接包含 AI 补全插件源码，回归验证响应解析与端点归一化不再崩溃。
 * 不发起任何真实网络请求。
 */

#include <glib.h>
#include <json-glib/json-glib.h>

#include "../src/plugins/ai-completion-plugin.c"

static void
test_error_detail_is_null_safe(void)
{
    GBytes *empty;
    gchar *detail;

    empty = g_bytes_new(NULL, 0);
    detail = ai_completion_error_detail(empty);
    g_assert_cmpstr(detail, ==, "");
    g_free(detail);
    detail = ai_completion_error_detail(NULL);
    g_assert_cmpstr(detail, ==, "");
    g_free(detail);
    g_bytes_unref(empty);
}

static void
test_extract_content_handles_empty_and_malformed(void)
{
    GError *error;
    gchar *result;

    error = NULL;
    result = ai_completion_extract_content(NULL, 0, &error);
    g_assert_null(result);
    g_assert_nonnull(error);
    g_clear_error(&error);

    result = ai_completion_extract_content("", 0, &error);
    g_assert_null(result);
    g_assert_nonnull(error);
    g_clear_error(&error);

    result = ai_completion_extract_content("not json", 8, &error);
    g_assert_null(result);
    g_assert_nonnull(error);
    g_clear_error(&error);

    result = ai_completion_extract_content("{\"error\":{\"message\":\"boom\"}}", -1, &error);
    g_assert_null(result);
    g_assert_nonnull(error);
    g_clear_error(&error);
}

static void
test_extract_content_parses_supported_shapes(void)
{
    GError *error;
    gchar *result;

    error = NULL;
    result = ai_completion_extract_content(
        "{\"choices\":[{\"message\":{\"content\":\" completion\"}}]}", -1, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(result, ==, " completion");
    g_free(result);

    result = ai_completion_extract_content(
        "{\"choices\":[{\"delta\":{\"content\":\" delta\"}}]}", -1, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(result, ==, " delta");
    g_free(result);

    result = ai_completion_extract_content(
        "{\"choices\":[{\"text\":\" text\"}]}", -1, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(result, ==, " text");
    g_free(result);

    result = ai_completion_extract_content(
        "{\"choices\":[{\"message\":{\"content\":[{\"type\":\"text\",\"text\":\" array text\"}]}}]}",
        -1,
        &error);
    g_assert_no_error(error);
    g_assert_cmpstr(result, ==, " array text");
    g_free(result);

    result = ai_completion_extract_content(
        "{\"choices\":[{\"message\":{\"content\":null}}]}", -1, &error);
    g_assert_null(result);
    g_assert_nonnull(error);
    g_clear_error(&error);
}

static void
test_inline_candidate_normalization(void)
{
    gchar *candidate;

    candidate = ai_completion_prepare_inline_candidate("  suffix");
    g_assert_cmpstr(candidate, ==, "  suffix");
    g_free(candidate);

    candidate = ai_completion_prepare_inline_candidate("\n\rnext line\nignored line");
    g_assert_cmpstr(candidate, ==, "next line");
    g_free(candidate);

    candidate = ai_completion_prepare_inline_candidate("\r\n");
    g_assert_null(candidate);

    candidate = ai_completion_prepare_inline_candidate(NULL);
    g_assert_null(candidate);
}

static void
test_endpoint_normalization(void)
{
    gchar *normalized;

    normalized = ai_completion_normalize_endpoint("https://api.deepseek.com");
    g_assert_cmpstr(normalized, ==, "https://api.deepseek.com/chat/completions");
    g_free(normalized);

    normalized = ai_completion_normalize_endpoint("https://api.deepseek.com/");
    g_assert_cmpstr(normalized, ==, "https://api.deepseek.com/chat/completions");
    g_free(normalized);

    normalized = ai_completion_normalize_endpoint("https://api.deepseek.com/v1");
    g_assert_cmpstr(normalized, ==, "https://api.deepseek.com/v1/chat/completions");
    g_free(normalized);

    normalized = ai_completion_normalize_endpoint("https://api.deepseek.com/v1/");
    g_assert_cmpstr(normalized, ==, "https://api.deepseek.com/v1/chat/completions");
    g_free(normalized);

    normalized = ai_completion_normalize_endpoint("https://api.deepseek.com/chat/completions");
    g_assert_cmpstr(normalized, ==, "https://api.deepseek.com/chat/completions");
    g_free(normalized);

    normalized = ai_completion_normalize_endpoint("https://api.deepseek.com/chat/completions/");
    g_assert_cmpstr(normalized, ==, "https://api.deepseek.com/chat/completions");
    g_free(normalized);

    normalized = ai_completion_normalize_endpoint("https://custom.example.com/api/v2/models/chat");
    g_assert_cmpstr(normalized, ==, "https://custom.example.com/api/v2/models/chat");
    g_free(normalized);

    normalized = ai_completion_normalize_endpoint("");
    g_assert_cmpstr(normalized, ==, "");
    g_free(normalized);
}

static void
test_build_body_uses_prefix_and_suffix(void)
{
    gchar *body;
    JsonParser *parser;
    JsonObject *object;
    JsonObject *message;
    const gchar *content;

    body = ai_completion_build_body("test-model", "int main", "{", "Existing summary");
    parser = json_parser_new();
    g_assert_true(json_parser_load_from_data(parser, body, -1, NULL));
    object = json_node_get_object(json_parser_get_root(parser));
    g_assert_cmpstr(json_object_get_string_member(object, "model"), ==, "test-model");
    message = json_array_get_object_element(json_object_get_array_member(object, "messages"), 1);
    content = json_object_get_string_member(message, "content");
    g_assert_nonnull(strstr(content, "<document_summary>"));
    g_assert_nonnull(strstr(content, "Existing summary"));
    g_assert_nonnull(strstr(content, "<fim_prefix>"));
    g_assert_nonnull(strstr(content, "int main"));
    g_assert_nonnull(strstr(content, "<fim_suffix>"));
    g_assert_nonnull(strstr(content, "{"));
    g_object_unref(parser);
    g_free(body);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vellum/ai-completion/error-detail-null-safe", test_error_detail_is_null_safe);
    g_test_add_func("/vellum/ai-completion/extract-empty-malformed", test_extract_content_handles_empty_and_malformed);
    g_test_add_func("/vellum/ai-completion/extract-shapes", test_extract_content_parses_supported_shapes);
    g_test_add_func("/vellum/ai-completion/inline-candidate-normalization", test_inline_candidate_normalization);
    g_test_add_func("/vellum/ai-completion/endpoint-normalization", test_endpoint_normalization);
    g_test_add_func("/vellum/ai-completion/body-prefix-suffix", test_build_body_uses_prefix_and_suffix);

    return g_test_run();
}
