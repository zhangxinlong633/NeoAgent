# Neo: portable C agent. HTTP via vendored BearHttpsClient (no libcurl).
# Build: make
# Run:   ./neo "your question"
#
# Layout: src/{cli,core,llm,capability,dag,memory,vendor}/
# Objects: build/ mirroring src/ (never write .o under src/)

CC      = cc
OBJDIR  = build
INCLUDES = -Isrc/core -Isrc/llm -Isrc/capability -Isrc/dag -Isrc/memory -Isrc/vendor
CFLAGS  = -O2 -Wall -Wextra $(INCLUDES)
LDFLAGS = -lm

SRC = \
	src/cli/main.c \
	src/core/config.c \
	src/core/daemon.c \
	src/core/neo_http.c \
	src/core/neo_md_term.c \
	src/llm/llm.c \
	src/capability/agent_tools.c \
	src/capability/command_tools.c \
	src/capability/capability_matrix.c \
	src/capability/capability_dir.c \
	src/capability/mcp_stdio.c \
	src/dag/dag.c \
	src/dag/dag_dir.c \
	src/dag/plan.c \
	src/memory/neo_embed.c \
	src/memory/neo_memory.c \
	src/vendor/yyjson.c \
	src/vendor/BearHttpsClientOne.c \
	src/vendor/md4c.c

OBJ = $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC))

neo: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Third-party amalgamations: silence noisy pedantic warnings.
$(OBJDIR)/vendor/yyjson.o: src/vendor/yyjson.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(OBJDIR)/vendor/BearHttpsClientOne.o: src/vendor/BearHttpsClientOne.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
		-Wno-sign-compare -c -o $@ $<

$(OBJDIR)/vendor/md4c.o: src/vendor/md4c.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-sign-compare -c -o $@ $<

clean:
	rm -rf $(OBJDIR)
	rm -f neo \
		tests/test_parse_commands \
		tests/test_command_exec \
		tests/test_parse_dags \
		tests/test_dag_loop \
		tests/test_dag_template \
		tests/test_dag_runner \
		tests/test_dag_on_fail \
		tests/test_plan_extract \
		tests/test_capability_matrix \
		tests/test_neo_embed \
		tests/test_neo_memory \
		tests/test_neo_md_term

# Shared flags for test binaries that compile vendor code from source.
TEST_CFLAGS = $(CFLAGS) -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare
TEST_VENDOR = src/vendor/yyjson.c src/vendor/BearHttpsClientOne.c
TEST_HTTP = src/core/neo_http.c

TEST_PARSE_CMD = tests/test_parse_commands
$(TEST_PARSE_CMD): tests/test_parse_commands.c src/core/config.c src/capability/capability_dir.c \
		src/dag/dag_dir.c src/core/config.h src/vendor/yyjson.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_parse_commands.c src/core/config.c \
		src/capability/capability_dir.c src/dag/dag_dir.c src/vendor/yyjson.c

TEST_CMD_EXEC = tests/test_command_exec
$(TEST_CMD_EXEC): tests/test_command_exec.c src/capability/command_tools.c src/core/config.c \
		src/capability/capability_dir.c src/dag/dag_dir.c \
		src/capability/command_tools.h src/core/config.h src/vendor/yyjson.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_command_exec.c src/capability/command_tools.c \
		src/core/config.c src/capability/capability_dir.c src/dag/dag_dir.c src/vendor/yyjson.c

TEST_PARSE_DAG = tests/test_parse_dags
$(TEST_PARSE_DAG): tests/test_parse_dags.c src/core/config.c src/capability/capability_dir.c \
		src/dag/dag_dir.c src/core/config.h src/vendor/yyjson.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_parse_dags.c src/core/config.c \
		src/capability/capability_dir.c src/dag/dag_dir.c src/vendor/yyjson.c

TEST_DAG_SRCS = src/dag/dag.c src/dag/dag_dir.c src/capability/agent_tools.c \
	src/capability/command_tools.c src/core/config.c $(TEST_HTTP) src/llm/llm.c \
	src/capability/capability_matrix.c src/capability/capability_dir.c src/capability/mcp_stdio.c \
	src/memory/neo_memory.c src/memory/neo_embed.c \
	$(TEST_VENDOR)

TEST_DAG_LOOP = tests/test_dag_loop
$(TEST_DAG_LOOP): tests/test_dag_loop.c $(TEST_DAG_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_dag_loop.c $(TEST_DAG_SRCS) $(LDFLAGS)

TEST_DAG_TMPL = tests/test_dag_template
$(TEST_DAG_TMPL): tests/test_dag_template.c $(TEST_DAG_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_dag_template.c $(TEST_DAG_SRCS) $(LDFLAGS)

TEST_DAG_DAG = tests/test_dag_runner
$(TEST_DAG_DAG): tests/test_dag_runner.c $(TEST_DAG_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_dag_runner.c $(TEST_DAG_SRCS) $(LDFLAGS)

TEST_DAG_ON_FAIL = tests/test_dag_on_fail
$(TEST_DAG_ON_FAIL): tests/test_dag_on_fail.c $(TEST_DAG_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_dag_on_fail.c $(TEST_DAG_SRCS) $(LDFLAGS)

TEST_PLAN = tests/test_plan_extract
$(TEST_PLAN): tests/test_plan_extract.c src/dag/plan.c $(TEST_DAG_SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_plan_extract.c src/dag/plan.c $(TEST_DAG_SRCS) $(LDFLAGS)

TEST_CAP_MATRIX = tests/test_capability_matrix
$(TEST_CAP_MATRIX): tests/test_capability_matrix.c src/capability/capability_matrix.c \
		src/capability/capability_dir.c src/dag/dag_dir.c src/capability/mcp_stdio.c \
		src/core/config.c $(TEST_HTTP) src/capability/agent_tools.c src/capability/command_tools.c \
		src/llm/llm.c src/memory/neo_memory.c src/memory/neo_embed.c $(TEST_VENDOR)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_capability_matrix.c src/capability/capability_matrix.c \
		src/capability/capability_dir.c src/dag/dag_dir.c src/capability/mcp_stdio.c \
		src/core/config.c $(TEST_HTTP) src/capability/agent_tools.c src/capability/command_tools.c \
		src/llm/llm.c src/memory/neo_memory.c src/memory/neo_embed.c $(TEST_VENDOR) $(LDFLAGS)

TEST_NEO_EMBED = tests/test_neo_embed
$(TEST_NEO_EMBED): tests/test_neo_embed.c src/memory/neo_embed.c src/memory/neo_embed.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_neo_embed.c src/memory/neo_embed.c $(LDFLAGS)

TEST_NEO_MEMORY = tests/test_neo_memory
$(TEST_NEO_MEMORY): tests/test_neo_memory.c src/memory/neo_memory.c src/memory/neo_embed.c \
		src/core/config.c src/capability/capability_dir.c src/dag/dag_dir.c $(TEST_VENDOR)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_neo_memory.c src/memory/neo_memory.c src/memory/neo_embed.c \
		src/core/config.c src/capability/capability_dir.c src/dag/dag_dir.c src/vendor/yyjson.c \
		$(LDFLAGS)

TEST_NEO_MD = tests/test_neo_md_term
$(TEST_NEO_MD): tests/test_neo_md_term.c src/core/neo_md_term.c src/vendor/md4c.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_neo_md_term.c src/core/neo_md_term.c src/vendor/md4c.c

test: $(TEST_PARSE_CMD) $(TEST_CMD_EXEC) $(TEST_PARSE_DAG) $(TEST_DAG_LOOP) $(TEST_DAG_TMPL) $(TEST_DAG_DAG) $(TEST_DAG_ON_FAIL) $(TEST_PLAN) $(TEST_CAP_MATRIX) $(TEST_NEO_EMBED) $(TEST_NEO_MEMORY) $(TEST_NEO_MD) neo
	./$(TEST_PARSE_CMD)
	./$(TEST_CMD_EXEC)
	./$(TEST_PARSE_DAG)
	./$(TEST_DAG_LOOP)
	./$(TEST_DAG_TMPL)
	./$(TEST_DAG_DAG)
	./$(TEST_DAG_ON_FAIL)
	./$(TEST_PLAN)
	./$(TEST_CAP_MATRIX)
	./$(TEST_NEO_EMBED)
	./$(TEST_NEO_MEMORY)
	./$(TEST_NEO_MD)
	./tests/cli_capability_matrix.sh

test-cli: neo
	./tests/cli_capability_matrix.sh

test-prompts-unit:
	cd tests/prompts/runner && python3 test_score.py && python3 test_judge.py

test-prompts-dry:
	python3 tests/prompts/runner/run.py --corpus tests/prompts/corpus --dry-run

test-prompts: test-prompts-unit
	python3 tests/prompts/runner/run.py --corpus tests/prompts/corpus --limit 50 --out tests/prompts/reports

.PHONY: clean test test-cli test-prompts test-prompts-unit test-prompts-dry
