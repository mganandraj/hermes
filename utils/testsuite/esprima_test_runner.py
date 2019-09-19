#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE
# file in the root directory of this source tree.

import argparse
import enum
import json
import subprocess
import tempfile
from os.path import isfile


@enum.unique
class TestStatus(enum.IntEnum):
    TEST_FAILED = 1
    TEST_PASSED = 2
    TEST_SKIPPED = 3
    TEST_TIMEOUT = 4


HERMES_LITERAL_NODE_TYPES = {
    "NullLiteral",
    "BooleanLiteral",
    "StringLiteral",
    "NumericLiteral",
    "RegExpLiteral",
}

# These are the keys in the JSON ASTs that should be omitted during diffing.
# values in these sets are keys to a child node, and they should always be
# omitted.
HERMES_OMITTED_KEYS_COMMON = {"typeAnnotation"}
ESPRIMA_OMITTED_KEYS_COMMON = {"loc", "range", "errors"}
# key is the type of a node, and value is the set of keys of a child node that
# needs to be omitted.
HERMES_OMITTED_KEYS = {
    "FunctionDeclaration": {"returnType"},
    "ArrayExpression": {"trailingComma"},
}
ESPRIMA_OMITTED_KEYS = {
    "Program": {"tokens", "sourceType", "comments"},
    "Literal": {"raw"},
    # ES6+ specific enhancement to the ESTree original definitions that Hermes
    # does not support yet.
    # TODO: remember to update or remove them once we update the parser.
    "FunctionDeclaration": {"async", "expression"},
    "FunctionExpression": {"async", "expression"},
    "ArrowFunctionExpression": {"async", "generator"},
    "Property": {"method", "shorthand"},
    "ForInStatement": {"each"},
}

# Collect the keys that should be omitted from either Hermes or esprima ASTs.
# For debugging purposes.
OMITTED_KEYS = {}
for k in list(HERMES_OMITTED_KEYS.keys()) + list(ESPRIMA_OMITTED_KEYS.keys()):
    OMITTED_KEYS[k] = set()
    if k in HERMES_OMITTED_KEYS:
        OMITTED_KEYS[k].update(HERMES_OMITTED_KEYS[k])
    if k in ESPRIMA_OMITTED_KEYS:
        OMITTED_KEYS[k].update(ESPRIMA_OMITTED_KEYS[k])

OMITTED_KEYS_COMMON = HERMES_OMITTED_KEYS_COMMON.union(ESPRIMA_OMITTED_KEYS_COMMON)

HERMES_TIMEOUT = 40
COMPILER_ARGS = ["-hermes-parser", "-dump-ast"]


class EsprimaTestRunner:
    def __init__(self, debug=False):
        self.debug = debug

    def printDebug(self, *args, **kwargs):
        if self.debug:
            print(*args, **kwargs)

    # process a hermes ast to make it more regular for diffing
    def process_hermes_ast(self, ast):
        if not isinstance(ast, dict):
            return ast
        if "type" not in ast:
            return ast
        if ast["type"] == "Empty":
            return None
        # skip top level 'File' node
        if ast["type"] == "File":
            ast = ast["program"]
        if ast["type"] == "ExpressionStatement":
            # esprima asts don't have the 'directive' field if it's null
            # if the string is 'use strict', populate the 'directive' field
            # TODO: is this intended in hermes or should we fix it?
            if (
                isinstance(ast["expression"], dict)
                and ast["expression"]["type"] == "StringLiteral"
                and ast["expression"]["value"] == "use strict"
            ):
                ast["directive"] = "use strict"
            else:
                del ast["directive"]
        # convert the literal node types to ESTree standard form
        if ast["type"] in HERMES_LITERAL_NODE_TYPES:
            if ast["type"] == "NullLiteral":
                ast["value"] = None
            if ast["type"] == "RegExpLiteral":
                ast["regex"] = {"pattern": ast["pattern"], "flags": ast["flags"]}
                del ast["pattern"]
                del ast["flags"]
            ast["type"] = "Literal"
        return ast

    # process an esprima ast to make it more regular for diffing
    def process_esprima_ast(self, ast):
        if isinstance(ast, dict):
            if "type" in ast and ast["type"] == "ExpressionStatement":
                # If the expression is a string, esprima will set the string to
                # the 'directive' field. Even if it's not 'use strict'
                if "directive" in ast and ast["directive"] != "use strict":
                    del ast["directive"]
            # If it is a regexp literal, the 'value' field is unnecessary and
            # Hermes does not have it.
            if "type" in ast and ast["type"] == "Literal" and "regex" in ast:
                del ast["value"]
            # If it is a template literal, the 'value' field contains
            # the 'cooked' and 'raw' strings, which should be moved.
            if "type" in ast and ast["type"] == "TemplateLiteral" and "quasis" in ast:
                for quasi in ast["quasis"]:
                    quasi["cooked"] = quasi["value"]["cooked"]
                    quasi["raw"] = quasi["value"]["raw"]
                    del quasi["value"]
        return ast

    def should_omit_esprima_key(self, node, key):
        return (key in ESPRIMA_OMITTED_KEYS_COMMON) or (
            "type" in node
            and node["type"] in ESPRIMA_OMITTED_KEYS
            and key in ESPRIMA_OMITTED_KEYS[node["type"]]
        )

    def hermes_should_omit_keys_count(self, node):
        omit_count = 0
        for elt in HERMES_OMITTED_KEYS_COMMON:
            if elt in node:
                omit_count += 1
        if "type" in node and node["type"] in HERMES_OMITTED_KEYS:
            for elt in HERMES_OMITTED_KEYS[node["type"]]:
                if elt in node:
                    omit_count += 1
        return omit_count

    # Compare two AST nodes from Hermes and ESPrima.
    # node1 is from hermes ast, node2 is from esprima ast
    def compare_nodes(self, node1, node2):
        node1 = self.process_hermes_ast(node1)
        node2 = self.process_esprima_ast(node2)
        if type(node1) != type(node2):
            self.printDebug("Expected {}, found {}".format(type(node2), type(node1)))
            return False
        if isinstance(node1, dict):
            expected_count = 0
            for key, val in node2.items():
                if self.should_omit_esprima_key(node2, key):
                    continue
                if key not in node1:
                    self.printDebug("{} missing property: {}", node1["type"], key)
                    return False
                expected_count += 1
                if not self.compare_nodes(node1[key], val):
                    return False
            # check if key-val pair counts match
            real_count = len(node1) - self.hermes_should_omit_keys_count(node1)
            if real_count != expected_count:
                self.printDebug(
                    "AST node element count mismatch in {}".format(node1["type"])
                )
                self.printDebug("Hermes count:   {}".format(real_count, node1))
                self.printDebug("Expected count: {}".format(expected_count))
                return False
            return True
        elif isinstance(node1, list):
            if len(node1) != len(node2):
                self.printDebug(
                    "List expected {} elements, found {}".format(len(node2), len(node1))
                )
                return False
            for i in range(len(node1)):
                if not self.compare_nodes(node1[i], node2[i]):
                    return False
            return True
        else:
            if node1 != node2:
                self.printDebug("Expected {}, found {}".format(node2, node1))
                return False
            return True

    # Remove nodes that should be omitted when diffing the outputs.
    # For debugging purposes.
    def trim_ast(self, node, is_hermes):
        if is_hermes:
            node = self.process_hermes_ast(node)
        else:
            node = self.process_esprima_ast(node)
        if isinstance(node, dict):
            tree = node
            res = {}
            for key, val in tree.items():
                if key in OMITTED_KEYS_COMMON:
                    continue
                if (
                    "type" in tree
                    and tree["type"] in OMITTED_KEYS
                    and key in OMITTED_KEYS[tree["type"]]
                ):
                    continue
                else:
                    res[key] = self.trim_ast(val, is_hermes)
            return res
        if isinstance(node, list):
            res = []
            for elt in node:
                res.append(self.trim_ast(elt, is_hermes))
            return res
        else:
            return node

    # Returns (TestStatus, error string)
    def diff_test_output(self, output, expected):
        try:
            test_passed = self.compare_nodes(json.loads(output), json.loads(expected))
            if not test_passed:
                if self.debug:
                    self.printDebug("ast not expected")
                    hermes_ast = json.dumps(
                        self.trim_ast(json.loads(output), True), sort_keys=True
                    )
                    self.printDebug("trimmed hermes ast:  ", hermes_ast)
                    expected_ast = json.dumps(
                        self.trim_ast(json.loads(expected), False), sort_keys=True
                    )
                    self.printDebug("trimmed expected ast:", expected_ast)
                return (TestStatus.TEST_FAILED, "ast not expected")
            return (TestStatus.TEST_PASSED, "")
        except json.JSONDecodeError as e:
            self.printDebug("JSON decode error:", e)
            return (TestStatus.TEST_FAILED, "json decode error")

    def get_expected_file_name(self, testfile):
        filename_stem = ""
        if testfile.endswith(".source.js"):
            filename_stem = testfile[:-10]
        else:
            if not testfile.endswith(".js"):
                raise TypeError("Not a valid test case.")
            # file name only ends with '.js'
            filename_stem = testfile[:-3]
        tree_file = filename_stem + ".tree.json"
        if isfile(tree_file):
            return tree_file
        failure_file = filename_stem + ".failure.json"
        if isfile(failure_file):
            return failure_file
        token_file = filename_stem + ".tokens.json"
        if isfile(token_file):
            return token_file
        self.printDebug("no expected file for:", testfile)
        raise TypeError("Can't find expected file.")

    # Run Hermes parser on the test and return the result from the subprocess.
    def parseSource(self, hermes, filename):
        # ".source.js" files has the format of "var source = \"...\";", and
        # the value of the 'source' variable should be the input to the parser.
        # So we evaluate the source with Hermes first and then parse the output.
        if ".source.js" in filename:
            with open(filename, "rb") as f:
                with tempfile.NamedTemporaryFile() as to_evaluate:
                    # append to the original source to print the 'source' variable.
                    for line in f:
                        to_evaluate.write(line)
                    to_evaluate.write(b"print(source);")
                    to_evaluate.flush()
                    with tempfile.NamedTemporaryFile() as evaluated:
                        # evaluate the source to get the actual test input.
                        evaluate_cmd = [hermes, to_evaluate.name]
                        evaluate_res = subprocess.run(
                            evaluate_cmd,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            timeout=HERMES_TIMEOUT,
                        )
                        # get rid of the newline added by print().
                        evaluated.write(evaluate_res.stdout.strip())
                        evaluated.flush()
                        # run the test through Hermes parser.
                        return subprocess.run(
                            [hermes] + COMPILER_ARGS + [evaluated.name],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            timeout=HERMES_TIMEOUT,
                        )
        else:
            return subprocess.run(
                [hermes] + COMPILER_ARGS + [filename],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=HERMES_TIMEOUT,
            )

    # There are three types of tests: correct tests, error tests, and token tests.
    # The files of expected output that ends with ".tree.json" has the expected AST,
    # so we expect to match the correct output; but a few of them have an 'errors'
    # field, which means esprima continues parsing despite the errors, but we expect
    # Hermes to exit with error.
    # TODO: The reason Hermes fails could be different from the expected error, so
    # we should find out a better way to handle these tests.
    # Files ends with ".failure.json" expect the parser to exit with error.
    # Files ends with ".tokens.json" expect the parser to produce tokens, which
    # Hermes does not support, so skip them for now.
    # Returns (TestStatus, error string)
    def run_test(self, filename, hermes):
        self.printDebug("testing", filename)
        expected_filename = self.get_expected_file_name(filename)
        try:
            res = self.parseSource(hermes, filename)
            self.printDebug("process return code", res.returncode)
            if expected_filename.endswith(".tree.json"):
                with open(expected_filename, "r") as expected_file:
                    expected_content = expected_file.read()
                    # If the expected file has an 'errors' field, Hermes parser should fail.
                    if '"errors":' in expected_content:
                        if res.returncode != 0:
                            return (TestStatus.TEST_PASSED, "expected failure")
                        else:
                            return (TestStatus.TEST_FAILED, "test should fail")
                    # If no errors, diff the output and expected.
                    return self.diff_test_output(
                        res.stdout.decode("utf-8"), expected_content
                    )
            elif expected_filename.endswith(".failure.json"):
                if res.returncode != 0:
                    return (TestStatus.TEST_PASSED, "expected failure")
                else:
                    return (TestStatus.TEST_FAILED, "test should fail")
            else:
                return (
                    TestStatus.TEST_SKIPPED,
                    "skip unsupported {} test".format(expected_filename.split(".")[-2]),
                )
        except subprocess.TimeoutExpired:
            return (TestStatus.TEST_TIMEOUT, "test time out")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=str, help="path to the dir of test fixtures")
    parser.add_argument(
        "--hermes", dest="hermes", type=str, help="path to the hermes binary"
    )
    parser.add_argument(
        "--debug",
        default=False,
        action="store_true",
        help="When a test fails, print debug information, like trimmed ASTs.",
    )
    args = parser.parse_args()
    runner = EsprimaTestRunner(args.debug)

    print(runner.run_test(args.path, args.hermes))


if __name__ == "__main__":
    main()
