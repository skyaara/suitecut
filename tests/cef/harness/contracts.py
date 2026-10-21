# SPDX-License-Identifier: MIT
"""Evidence contracts remain active under Python -O; JSON coercion is forbidden."""
import json
from pathlib import PurePosixPath


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f'Duplicate JSON key: {key}')
            result[key] = value
        return result

    def invalid(value):
        raise ValueError(f'Non-finite JSON number: {value}')

    return json.loads(raw, object_pairs_hook=pairs, parse_constant=invalid)


def validate_contract(contract):
    require(isinstance(contract, dict) and contract.keys() == {'file', 'rules'}, 'Evidence requires file and rules')
    file = contract['file']
    require(isinstance(file, str) and file and not PurePosixPath(file).is_absolute() and '..' not in PurePosixPath(file).parts and '\\' not in file and file.endswith('.json'), 'Evidence file must be a confined relative JSON path')
    rules = contract['rules']
    require(isinstance(rules, list) and bool(rules), 'Evidence rules must be nonempty')
    for rule in rules:
        require(isinstance(rule, dict) and 'path' in rule and len(rule) == 2, 'Each rule needs path and exactly one predicate')
        require(isinstance(rule['path'], str) and rule['path'] and all(rule['path'].split('.')), 'Invalid evidence field path')
        operation = next(k for k in rule if k != 'path')
        value = rule[operation]
        require(operation in ('equals', 'minItems', 'nonEmptyString', 'allTrue', 'allowedItems'), f'Unknown evidence predicate: {operation}')
        if operation == 'minItems':
            require(type(value) is int and value > 0, 'minItems must be a positive integer')
        elif operation == 'nonEmptyString':
            require(value is True, 'nonEmptyString must be true')
        elif operation == 'allowedItems':
            require(isinstance(value, list) and bool(value) and all(isinstance(item, str) and item for item in value), 'allowedItems requires a nonempty string allowlist')
        elif operation == 'allTrue':
            require(isinstance(value, str) and bool(value), 'allTrue requires a success field name')


def check_contract(value, contract):
    validate_contract(contract)
    require(isinstance(value, dict), 'Evidence report must be an object')
    for key in ('passed', 'completed', 'ok'):
        require(value.get(key) is not False, f'{key}=false')
    for key in ('failures', 'cleanupFailures', 'cleanupErrors', 'unhandled'):
        if key in value:
            require(value[key] == [], f'Nonempty or malformed {key}')
    for rule in contract['rules']:
        current = value
        for key in rule['path'].split('.'):
            require(isinstance(current, dict) and key in current, f'Missing evidence field: {rule["path"]}')
            current = current[key]
        if 'equals' in rule:
            expected = rule['equals']
            require(type(current) is type(expected) and current == expected, f'{rule["path"]}: expected {expected!r}, got {current!r}')
        elif 'minItems' in rule:
            require(isinstance(current, list) and len(current) >= rule['minItems'], f'{rule["path"]}: missing required assertion coverage (minimum {rule["minItems"]})')
            for item in current:
                require(isinstance(item, (str, dict)) and bool(item), f'{rule["path"]}: invalid assertion receipt')
                if isinstance(item, dict):
                    require(all(item.get(k) is not False for k in ('passed', 'ok')), f'{rule["path"]}: failed assertion receipt')
        elif 'nonEmptyString' in rule:
            require(isinstance(current, str) and bool(current.strip()), f'{rule["path"]}: expected nonempty string')
        elif 'allowedItems' in rule:
            require(isinstance(current, list) and all(isinstance(item, str) and item in rule['allowedItems'] for item in current), f'{rule["path"]}: undeclared unexercised coverage')
        elif 'allTrue' in rule:
            require(isinstance(current, list) and bool(current) and all(isinstance(item, dict) and item.get(rule['allTrue']) is True for item in current), f'{rule["path"]}: missing or failed scenario result')
    return len(contract['rules'])
