#!/usr/bin/env python3
"""Khronos OpenGL gl.xml to C++ GL wrapper generator."""

import argparse
import json
import os
import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from config import (
    EXTENSION_SUFFIXES,
    RESERVED_NAMES,
    FUNCTION_SUFFIXES,
    HANDLE_TYPES,
    EXCLUDED_ENUMS,
    EXTRA_ENUM_GROUPS
)
import templates
import util

class ParsedNode:

    """XML element parsed into a node."""

    def _parse_elem_text(self, generator, elem):
        """Parse XML element."""
        if elem.tag == 'ptype':
            self.ptype = elem.text
            if self.ptype == 'GLbitfield' and self.group_type == 'bitmask':
                self.enum_type = self.group
                self.node_type = generator.to_type_name(self.enum_type)
            elif self.ptype == 'GLenum':
                if self.group == '':
                    self.node_type = self.ptype
                else:
                    self.enum_type = self.group
                    self.node_type = generator.to_type_name(self.enum_type)
                    generator.used_enum_groups.add(self.group)
            else:
                self.node_type = self.ptype
            self.wrapper_type.append(self.node_type)
            self.native_type.append(self.ptype)
        elif elem.tag == 'name':
            self.node_name = elem.text
        else:
            print(f"Warning: Unknown node element: '{elem.tag}'")

    def __init__(self, generator, node):
        """Contructor for XML element parsed node."""
        self.group        = ''
        self.node_type    = ''
        self.node_name    = ''
        self.enum_type    = ''
        self.group_type   = 'basic'
        self.ptype        = ''
        self.wrapper_type = []
        self.native_type  = []

        if 'group' in node.attrib:
            self.group = node.attrib['group']

        if self.group and self.group in generator.bitmask_groups:
            self.group_type = 'bitmask'
            generator.used_enum_groups.add(self.group)

        if node.text:
            self.wrapper_type.append(node.text)
            self.native_type.append(node.text)

        for elem in node:
            if elem.text:
                self._parse_elem_text(generator, elem)
            if elem.tail:
                self.wrapper_type.append(elem.tail)
                self.native_type.append(elem.tail)

class Node:

    """Node is either one argument to Khronos API function or return value."""

    def __init__(self, generator, node):
        """Contructor for node."""
        parsed = ParsedNode(generator, node)

        self.group        = parsed.group
        self.wrapper_type = ''.join(parsed.wrapper_type).strip()
        self.native_type  = ''.join(parsed.native_type).strip()
        self.name         = parsed.node_name
        self.node_type    = parsed.node_type
        self.element_type = parsed.ptype
        self.is_pointer   = False

        # Patch internalformat as special case
        self.format_string = f'{parsed.node_name} = {{}}'
        if (parsed.group == 'InternalFormat' and parsed.ptype == 'GLint'):
            # Treat like enum type;
            # Cast native type to strongly typed enum type and use c_str()
            format_entry = 'gl::c_str(static_cast<gl::{0}>({{}}))'.format(
                generator.to_type_name(parsed.group)
            )
        elif (
                parsed.ptype in generator.pointer_types
                or '*' in self.native_type
                or parsed.ptype in HANDLE_TYPES
        ):
            # Pointer types are formatted with fmt::ptr()
            self.is_pointer = True
            format_entry = 'fmt::ptr(reinterpret_cast<const void*>({}))'
        elif parsed.enum_type != '':
            if (
                    parsed.ptype == 'GLbitfield'
                    or parsed.enum_type in generator.bitmask_enums
            ):
                # Bitmask types use to_string() which builds a temporary string
                format_entry = 'gl::to_string({})'
            else:
                # Enum types:
                # Cast native type to strongly typed enum type and use c_str()
                #format_entry = 'gl::c_str({})'

                format_entry = 'gl::c_str(static_cast<gl::{0}>({{}}))'.format(
                    generator.to_type_name(parsed.group)
                )

        elif parsed.node_type == 'GLboolean':
            format_entry = 'gl::c_str({})'
        elif parsed.node_type == 'GLbitfield':
            format_entry = "{}"
        elif parsed.ptype == 'GLenum':
            format_entry = 'gl::enum_string({})'
        elif parsed.ptype:
            format_entry = "{}"
        else:
            format_entry  = ''
            self.format_string = ''

        self.format_entry = format_entry.format(parsed.node_name)

class GLGenerator:

    """Convert Khronos gl.xml to C++ GL wrapper."""

    def _read_json(self, filename):
        """Read json file relative to script path."""
        path = os.path.join(self.script_path, filename)
        try:
            with open(path) as file:
                return json.load(file)
        except Exception as exception:
            print('Error parsing {}: {}'.format(path, str(exception)))
            raise

    def _choose_enum_names(self, items):
        """Pick enum name."""
        suffix_items = set()
        non_suffix_items = set()
        for item in sorted(items):
            suffix_found = False
            for suffix in EXTENSION_SUFFIXES:
                if item.endswith(suffix):
                    suffix_found = True
                    suffix_items.add((item, item[:-len(suffix)]))
                    break

            if not suffix_found:
                non_suffix_items.add(item)

        res = set()
        for item_tuple in suffix_items:
            match_found = False
            for non_suffix in non_suffix_items:
                if item_tuple[1] == non_suffix:
                    res.add(item_tuple[1])
                    match_found = True
                    break
            if not match_found:
                if item_tuple[0] in self.enum_list:
                    res.add(item_tuple[0])

        for non_suffix in non_suffix_items:
            if non_suffix in self.enum_list:
                res.add(non_suffix)

        return sorted(res)

    @staticmethod
    def split_to_body_and_ext(text):
        """Split GL extension name to body and extension."""
        for suffix in EXTENSION_SUFFIXES:
            suffix = suffix[1:]
            if text.endswith(suffix):
                return (text[:-len(suffix)], suffix)
        return (text, '')

    def to_type_name(self, text) -> str:
        """Generate wrapper name for type."""
        return util.to_snake_case(self.split_to_body_and_ext(text)[0]).capitalize()

    @staticmethod
    def wrapper_function_name(text):
        """Generate wrappe name for function."""
        text = GLGenerator.split_to_body_and_ext(text)
        body = text[0]
        ext  = text[1]
        for suffix, replacement in FUNCTION_SUFFIXES.items():
            if body.endswith(suffix):
                body = body[:-len(suffix)] + replacement
                break
        text = body + ext
        res = util.to_snake_case(text[2:])
        return res

    def __init__(self, outpath):
        """Constructor for GLGenerator."""
        self.extensions_to_collect = [
            'GL_ARB_bindless_texture',
            'GL_ARB_sparse_texture',
            'GL_ARB_sparse_texture2',
            'GL_ARB_sparse_texture_clamp',
            'GL_KHR_parallel_shader_compile'
        ]
        self.script_path = os.path.dirname(os.path.realpath(__file__))
        self.outpath     = outpath
        self.func_prefix = 'gl'
        self.all_enum_string_cases                     = []
        self.command_list                              = []
        self.command_required_by_feature               = defaultdict(list)
        self.command_removed_by_feature                = defaultdict(list)
        self.command_required_by_extension             = defaultdict(list)
        self.command_enum_declarations                 = ''
        self.command_map_entries                       = ''
        self.command_case_entries                      = ''
        self.command_info_entries                      = ''
        self.extensions                                = []
        self.extension_enum_declarations               = ''
        self.extension_map_entries                     = ''
        self.extension_case_entries                    = ''
        self.enum_helper_definitions                   = []
        self.enum_list                                 = []
        self.enum_required_by_feature                  = defaultdict(list)
        self.enum_removed_by_feature                   = defaultdict(list)
        self.enum_required_by_extension                = defaultdict(list)
        self.enum_name_to_value_str                    = {} # key: string, value: string
        self.enum_name_to_value                        = {} # key: string, value: int
        self.enum_string_function_declarations         = []
        self.enum_string_function_definitions          = []
        self.untyped_enum_string_function_declarations = []
        self.untyped_enum_string_function_definitions  = []
        self.enum_to_base_zero_function_declarations   = []
        self.base_zero_to_enum_function_declarations   = []
        self.enum_base_zero_function_definitions       = []
        self.get_proc_address_calls                    = []
        self.group_to_enum_list                        = defaultdict(list)
        self.wrapper_function_declarations             = []
        self.wrapper_function_definitions              = []
        self.wrapper_enum_declarations                 = []
        self.used_enum_groups                          = set()
        self.bitmask_groups                            = []
        self.bitmask_enums                             = []
        self.map_make_entries                          = []
        self.dynamic_function_declarations             = []
        self.dynamic_function_get_statements           = []
        self.dynamic_function_definitions              = []
        self.pointer_types                             = []
        self.roots                                     = []

        for filename in [ 'gl.xml', 'gl_extra.xml' ]:
            try:
                xml_path = os.path.join(self.script_path, filename)
                tree = ET.parse(xml_path)
                self.roots.append(tree.getroot())
            except Exception as exception:
                print('Error parsing {}: {}'.format(xml_path, str(exception)))
                raise

    @staticmethod
    def get_text(node) -> str:
        """Recursively build strint contents from XML node."""
        result = ''
        if node.text:
            result = node.text
        for elem in node:
            result += GLGenerator.get_text(elem)
        if node.tail:
            result += node.tail
        return result

    @staticmethod
    def get_name(node) -> str:
        """Get name for XML node."""
        if 'name' in node.attrib:
            return node.attrib['name']
        for elem in node:
            if elem.tag == 'name':
                return elem.text
        return ''

    def _parse_types(self):
        """Parse GL types from XML."""
        for root in self.roots:
            for types in root.iter('types'):
                for node in types.iter('type'):
                    type_name = GLGenerator.get_name(node)
                    text = GLGenerator.get_text(node).strip()
                    if '*' in text and not text.startswith('struct'):
                        self.pointer_types.append(type_name)

    def _parse_groups(self):
        """Parse GL enum groups from XML."""
        for root in self.roots:
            for group in root.iter('group'):
                group_name = group.attrib.get('name', '')
                for enum in group.iter('enum'):
                    enum_name = enum.attrib.get('name', '')
                    self.group_to_enum_list[group_name].append(enum_name)

    def _parse_enums(self):
        """Parse GL enums from XML."""
        for root in self.roots:
            for enums in root.iter('enums'):
                enums_group = enums.attrib.get('group', '')
                group_type = enums.attrib.get('type', '')

                for enum in enums.iter('enum'):
                    value_str  = enum.attrib['value']
                    enum_value = util.to_int(value_str)
                    enum_name  = enum.attrib['name']
                    enum_type  = enum.attrib.get('type', '')

                    if enum_name in EXCLUDED_ENUMS:
                        continue

                    if enums_group:
                        group = enums_group
                    else:
                        group = enum.attrib.get('group', '')

                    if group_type == 'bitmask':
                        self.bitmask_groups.append(group)
                        self.bitmask_enums.append(enum_name)
                    elif enum_type == 'bitmask':
                        self.bitmask_enums.append(enum_name)

                    self.enum_name_to_value[enum_name] = enum_value
                    self.enum_name_to_value_str[enum_name] = value_str
                    self.group_to_enum_list[group].append(enum_name)

    def _parse_features(self):
        """Parse GL features from XML."""
        for root in self.roots:
            for feature in root.iter('feature'):
                api = feature.attrib.get('api', '')
                feature_name = feature.attrib.get('name', '')
                feature_number = int(float(feature.attrib.get('number', '')) * 10.0)

                # filter by api
                if api != 'gl':
                    continue

                for require in feature.iter('require'):
                    require_profile = require.attrib.get('profile', '')
                    if require_profile and require_profile != 'core':
                        # filter by profile
                        continue

                    for enum in require.iter('enum'):
                        enum_name = enum.attrib.get('name', '')
                        self.enum_list.append(enum_name)
                        self.enum_required_by_feature[enum_name].append({
                            'api':     api,
                            'name':    feature_name,
                            'number':  feature_number,
                            'profile': require_profile
                        })
                    for command in require.iter('command'):
                        command_name = command.attrib['name']
                        self.command_list.append(command_name)
                        self.command_required_by_feature[command_name].append({
                            'api':     api,
                            'name':    feature_name,
                            'number':  feature_number,
                            'profile': require_profile
                        })

                for remove in feature.iter('remove'):
                    remove_profile = remove.attrib.get('profile', '')
                    if require_profile and require_profile != 'core':
                        # filter by profile
                        continue

                    for enum in remove.iter('enum'):
                        enum_name = enum.attrib.get('name', '')
                        self.enum_removed_by_feature[enum_name].append({
                            'api':     api,
                            'name':    feature_name,
                            'number':  feature_number,
                            'profile': remove_profile
                        })
                    for command in remove.iter('command'):
                        command_name = command.attrib['name']
                        self.command_removed_by_feature[command_name].append({
                            'api':     api,
                            'name':    feature_name,
                            'number':  feature_number,
                            'profile': remove_profile
                        })

    ext_re = re.compile(r'GL_([0-9A-Z]+)_[0-9a-zA-Z_]*')
    def _parse_extensions(self):
        """Parse GL extensions from XML."""
        for root in self.roots:
            for extensions in root.iter('extensions'):
                for extension in extensions.iter('extension'):
                    extension_name = extension.attrib.get('name', '')
                    #print(f'Extension: {extension_name}')
                    self.extensions.append(extension_name)

                    extension_apis = extension.attrib.get('supported', '')
                    extension_api_list = set(extension_apis.split('|'))

                    # filter by api
                    if 'gl' not in extension_apis:
                        continue

                    for require in extension.iter('require'):
                        for enum in require.iter('enum'):
                            enum_name = enum.attrib.get('name', '')
                            self.enum_list.append(enum_name)
                            self.enum_required_by_extension[enum_name].append({
                                "name":     extension_name,
                                "api_list": extension_api_list})
                        for command in require.iter('command'):
                            command_name = command.attrib['name']
                            self.command_list.append(command_name)
                            self.command_required_by_extension[command_name].append({
                                "name":     extension_name,
                                "api_list": extension_api_list})

    def _add_extra_enums(self):
        """Add extra enums from EXTRA_ENUM_GROUPS."""
        for group_name, group in EXTRA_ENUM_GROUPS.items():
            self.used_enum_groups.add(group_name)
            for enum in group:
                self.enum_list.append(enum)
                self.group_to_enum_list[group_name].append(enum)

    def _parse_node(self, node):
        """Parse XML node."""
        return Node(self, node)

    def _case_value(self, enum_name) -> str:
        """Generate enum name case value."""
        return self.enum_name_to_value_str[enum_name]

    @staticmethod
    def get_command_name(command_element) -> str:
        """Get name for GL command."""
        proto_element = command_element.find('proto')
        for name in proto_element.iter('name'):
            return name.text
        return ''

    def _collect_command(self, command_element):
        """Collect GL command information."""
        command_name = GLGenerator.get_command_name(command_element)
        #print(f'Command: {command_name}')

        if command_name not in self.command_list:
            return

        if not self._command_should_collect(command_name):
            return

        func_prefix_len = len(self.func_prefix)

        proto_element            = command_element.find('proto')
        proto_info               = self._parse_node(proto_element)
        native_name              = proto_info.name
        short_name               = native_name[func_prefix_len:]
        wrapper_name             = GLGenerator.wrapper_function_name(command_name)
        native_return_type       = proto_info.native_type
        wrapper_return_type      = proto_info.wrapper_type
        capture_result           = ''
        native_return_statement  = ''
        wrapper_return_statement = ''
        if native_return_type != 'void':
            capture_result           = 'auto res = '
            native_return_statement  = '    return res;\n'
            wrapper_return_statement = f'    return static_cast<{wrapper_return_type}>(res);\n'

        native_params           = []
        wrapper_params          = []
        format_strings          = []
        format_entries          = []
        argument_list           = []
        native_arg_type_list    = []
        wrapper_arg_type_list   = []

        for param in command_element.findall('param'):
            param_info = self._parse_node(param)
            param_name = param_info.name
            native_params.append(param_info.native_type + ' ' + param_name)
            wrapper_params.append(param_info.wrapper_type + ' ' + param_name)
            native_arg_type_list.append(param_info.native_type)
            wrapper_arg_type_list.append(param_info.wrapper_type)
            format_strings.append(param_info.format_string)
            format_entries.append(param_info.format_entry)
            if param_info.is_pointer:
                argument_list.append(f'reinterpret_cast<{param_info.native_type}>({param_name})')
            else:
                argument_list.append(f'static_cast<{param_info.native_type}>({param_name})')

        log_format_entries = ''
        if len(format_entries) > 0:
            separator = ',\n        '
            log_format_entries = separator + separator.join(format_entries)

        formatting = {
            'COMMAND_NAME':              command_name,
            'SHORT_NAME':                short_name,
            'NATIVE_RETURN_TYPE':        native_return_type.strip(),
            'NATIVE_NAME':               native_name,
            'NATIVE_ARGUMENTS':          ', '.join(native_params),
            'NATIVE_ARG_TYPE_LIST':      ', '.join(native_arg_type_list),
            'NATIVE_RETURN_STATEMENT':   native_return_statement,
            'COMMAND_VERSION':           self._command_version(command_name),

            'WRAPPER_RETURN_TYPE':       wrapper_return_type.strip(),
            'WRAPPER_NAME':              wrapper_name,
            'WRAPPER_ARGUMENTS':         ', '.join(wrapper_params),
            'WRAPPER_ARG_TYPE_LIST':     ', '.join(wrapper_arg_type_list),
            'WRAPPER_RETURN_STATEMENT':  wrapper_return_statement,

            'LOG_FORMAT_STRING':         ', '.join(format_strings),
            'LOG_FORMAT_ENTRIES':        log_format_entries,
            'CAPTURE_RESULT':            capture_result,
            'ARGUMENT_LIST':             ', '.join(argument_list),
        }

        wrapper_function_declaration = templates.WRAPPER_FUNCTION_DECLARATION.format(**formatting)
        self.wrapper_function_declarations.append(wrapper_function_declaration)

        wrapper_function_definition = templates.WRAPPER_FUNCTION_DEFINITION.format(**formatting)
        self.wrapper_function_definitions.append(wrapper_function_definition)

        for feature in self.command_required_by_feature.get(command_name, []):
            number = feature["number"]
            self.command_info_entries += (
                f'    check_version(Command::Command_{command_name}, {number});\n'
            )

        for extension in self.command_required_by_extension.get(command_name, []):
            extension_name = extension["name"]
            self.command_info_entries += (
                f'    check_extension(Command::Command_{command_name}, '
                f'Extension::Extension_{extension_name});\n'
            )

        decl_entry = templates.DYNAMIC_LOAD_FUNCTION_DECLARATION  .format(**formatting)
        defn_entry = templates.DYNAMIC_LOAD_FUNCTION_DEFINITION   .format(**formatting)
        get_entry  = templates.DYNAMIC_LOAD_FUNCTION_GET_STATEMENT.format(**formatting)
        self.dynamic_function_declarations  .append(decl_entry)
        self.dynamic_function_definitions   .append(defn_entry)
        self.dynamic_function_get_statements.append(get_entry)

    def _parse_and_build_commands(self):
        """Parse and process GL commands from XML."""
        for root in self.roots:
            for commands in root.iter('commands'):
                for command_element in commands.iter('command'):
                    try:
                        self._collect_command(command_element)

                    except Exception as exception:
                        command_name = GLGenerator.get_command_name(command_element)
                        print('Error processing command {}: {}'.format(command_name, str(exception)))
                        raise

        extension_name_max_len = 0
        for extension in self.extensions:
            extension_name_max_len = max(extension_name_max_len, len(extension))

        enum_value   = 1
        declarations = []
        map_entries  = []
        case_entries = []

        for extension in sorted(set(self.extensions)):
            quoted_extension = '"' + extension + '"'
            declaration  = f'    Extension_{extension:{extension_name_max_len}} = {enum_value:>6}'
            map_entry    = '    g_extension_map.insert(std::pair<std::string, Extension>({0:{1}}, Extension::Extension_{2:{3}}));'.format(
                quoted_extension, extension_name_max_len + 2, extension, extension_name_max_len
            )
            case_entry = '        case Extension::Extension_{0:{1}}: return "{0}";'.format(
                extension, extension_name_max_len
            )
            declarations.append(declaration)
            map_entries.append (map_entry)
            case_entries.append(case_entry)
            enum_value += 1

        declarations.append(f'    Extension_Count = {enum_value:>6}')
        self.extension_enum_declarations = ',\n'.join(declarations)
        self.extension_map_entries       = '\n'.join(map_entries)
        self.extension_case_entries      = '\n'.join(case_entries)

        commands = set(self.command_list)

        commands = sorted(commands)

        command_name_max_len = 0
        for command in commands:
            command_name_max_len = max(command_name_max_len, len(command))

        enum_value   = 1
        declarations = []
        map_entries  = []
        case_entries = []
        for command in commands:
            declaration = f'    Command_{command:{command_name_max_len}} = {enum_value:>6}'
            map_entry   = '    g_command_map.insert(std::pair<std::string, Command>({0:{1}}, Command::Command_{2:{1}}));'.format(
                '"' + command + '"', command_name_max_len, command
            )
            case_entry  = '        case Command::Command_{0:{1}}: return "{0}";'.format(
                command, command_name_max_len
            )
            declarations.append(declaration)
            map_entries.append (map_entry)
            case_entries.append(case_entry)
            enum_value += 1

        declarations.append('    Command_Count = {:>6}'.format(enum_value))
        self.command_enum_declarations = ',\n'.join(declarations)
        self.command_map_entries       = '\n'.join(map_entries)
        self.command_case_entries      = '\n'.join(case_entries)

    def _enum_should_collect(self, enum):
        """Check if GL enum should be collected. Is required and not removed."""
        last_require_version = 0
        for feature in self.enum_required_by_feature[enum]:
            last_require_version = max(last_require_version, feature['number'])

        last_remove_version = 0
        for feature in self.enum_removed_by_feature[enum]:
            last_remove_version = max(last_remove_version, feature['number'])

        for extension in self.enum_required_by_extension[enum]:
            extension_name = extension['name']
            if extension_name in self.extensions_to_collect:
                #print(f'Collecting enum {enum} because it is required by {extension_name}')
                return True

        # filter by command not required by core profile
        if last_require_version == 0:
            return False

        # filter by removed
        if last_remove_version > last_require_version:
            return False

        return True

    def _enum_version(self, name):
        """Get GL enum version."""
        last_remove_version = 0
        for feature in self.enum_removed_by_feature[name]:
            last_remove_version = max(last_remove_version, feature['number'])

        earliest_non_remove_version_number = 9999

        for feature in self.enum_required_by_feature[name]:
            number = feature['number']
            if number > last_remove_version:
                if number < earliest_non_remove_version_number:
                    earliest_non_remove_version_number = number
                    return feature['name']

        for extension in self.enum_required_by_extension[name]:
            extension_name = extension['name']
            if extension_name in self.extensions_to_collect:
                return extension_name

        return '?'

    def _command_version(self, name):
        """Get GL command version."""
        last_remove_version = 0
        for feature in self.command_removed_by_feature[name]:
            last_remove_version = max(last_remove_version, feature['number'])

        earliest_non_remove_version_number = 9999

        for feature in self.command_required_by_feature[name]:
            number = feature['number']
            if number > last_remove_version:
                if number < earliest_non_remove_version_number:
                    earliest_non_remove_version_number = number
                    return feature['name']

        for extension in self.command_required_by_extension[name]:
            extension_name = extension['name']
            if extension_name in self.extensions_to_collect:
                return extension_name

        return '?'

    def _command_should_collect(self, command_name):
        """Check if GL command should be collected: required and not removed."""
        last_require_version = 0
        for feature in self.command_required_by_feature[command_name]:
            last_require_version = max(last_require_version, feature['number'])

        last_remove_version = 0
        for feature in self.command_removed_by_feature[command_name]:
            last_remove_version = max(last_remove_version, feature['number'])

        for extension in self.command_required_by_extension[command_name]:
            extension_name = extension['name']
            if extension_name in self.extensions_to_collect:
                #print(f'Collecting command {command_name} because it is required by {extension_name}')
                return True

        # filter by command not required by core profile
        if last_require_version == 0:
            return False

        # filter by removed
        if last_remove_version > last_require_version:
            return False

        return True

    def _build_all_enums(self):
        """Parse and process GL enums."""
        uniq_enums = []
        used_values = set()
        enum_value_to_name_list = defaultdict(set) # key: int,    value: list of strings (enum name
        for enum in self.enum_list:
            if enum in self.bitmask_enums:
                continue

            if enum not in self.enum_name_to_value:
                print(f'Notice: enum {enum} has no value')
                continue

            if not self._enum_should_collect(enum):
                continue

            value = self.enum_name_to_value[enum]

            enum_value_to_name_list[value].add(enum)

            if value in used_values:
                continue

            uniq_enums.append((value, enum))
            used_values.add(value)

        uniq_enums.sort()

        for value, enum in uniq_enums:
            name_list = self._choose_enum_names(enum_value_to_name_list[value])
            if name_list:
                list_str = ' / '.join(name_list)
                enum_value_str = self._case_value(enum)
                case = f'        case {enum_value_str}: return "{list_str}";'
                self.all_enum_string_cases.append(case)

                self.map_make_entries.append(f'    {{ "{enum}", {enum_value_str} }}')

    def _build_enum_groups(self):
        """Parse and process GL enums by."""
        for group in self.used_enum_groups:
            if group not in self.group_to_enum_list:
                print(f'Warning: Enum group {group} has no values defined.')
                continue

            group_type = 'basic'
            if group in self.bitmask_groups:
                group_type = 'bitmask'

            values_used     = set()
            enum_name_list  = self.group_to_enum_list[group]
            enum_tuple_list = []
            for enum_name in enum_name_list:
                if enum_name not in self.enum_list:
                    continue

                if enum_name not in self.enum_name_to_value:
                    print(f'Warning: enum {enum_name} has no value')
                    continue

                if not self._enum_should_collect(enum_name):
                    continue

                enum_value = self.enum_name_to_value[enum_name]
                if enum_value not in values_used:
                    enum_value_str = self._case_value(enum_name)
                    enum_tuple_list.append((enum_value, enum_value_str, enum_name))
                    values_used.add(enum_value)

                    if enum_name in self.bitmask_enums:
                        group_type = 'bitmask'

            enum_tuple_list.sort()

            group_max_len = 0
            for enum_info in enum_tuple_list:
                wrapper_enum_value_name = enum_info[2][3:].lower()
                if wrapper_enum_value_name in RESERVED_NAMES:
                    wrapper_enum_value_name = wrapper_enum_value_name + '_'
                if len(wrapper_enum_value_name) > group_max_len:
                    group_max_len = len(wrapper_enum_value_name)

            group_enum_string_entries            = []
            group_enum_to_base_zero_entries      = []
            base_zero_to_group_enum_entries      = []
            group_wrapper_enum_value_definitions = []
            base_zero_value = 0

            for enum_info in enum_tuple_list:
                wrapper_enum_value_name = enum_info[2][3:].lower()

                if wrapper_enum_value_name in RESERVED_NAMES:
                    wrapper_enum_value_name = wrapper_enum_value_name + '_'

                formatting = {
                    'ENUM_VALUE':              enum_info[1],
                    'ENUM_STRING':             enum_info[2],
                    'ENUM_BASE_ZERO_VALUE':    base_zero_value,
                    'ENUM_VERSION':            self._enum_version(enum_info[2]),
                    'WRAPPER_ENUM_TYPE_NAME':  self.to_type_name(group),
                    'WRAPPER_ENUM_VALUE_NAME': wrapper_enum_value_name,
                }

                string_entry = templates.ENUM_STRING_MAKE_ENTRY[group_type].format(**formatting)
                group_enum_string_entries.append(string_entry)
                group_wrapper_enum_value_definitions.append(
                    '    {:{}} = {:>6}u /* {} */'.format(
                        wrapper_enum_value_name,
                        group_max_len,
                        enum_info[1],
                        self._enum_version(enum_info[2])))

                if group_type == 'basic':
                    group_enum_to_base_zero_make_entry = templates.ENUM_TO_BASE_ZERO_MAKE_ENTRY.format(**formatting)
                    base_zero_to_group_enum_make_entry = templates.BASE_ZERO_TO_ENUM_MAKE_ENTRY.format(**formatting)
                    group_enum_to_base_zero_entries.append(group_enum_to_base_zero_make_entry)
                    base_zero_to_group_enum_entries.append(base_zero_to_group_enum_make_entry)
                    base_zero_value = base_zero_value + 1

            if enum_tuple_list:
                wrapper_enum_name = self.to_type_name(group)
                definitions       = ',\n'.join(sorted(group_wrapper_enum_value_definitions))
                string_entries    = '\n'.join(sorted(group_enum_string_entries))
                formatting = {
                    'WRAPPER_ENUM_TYPE_NAME':          wrapper_enum_name,
                    'WRAPPER_ENUM_STRING_FN_NAME':     self.split_to_body_and_ext(group)[0],
                    'GROUP_NAME':                      group,
                    'WRAPPER_ENUM_VALUE_DEFINITIONS':  definitions,
                    'GROUP_ENUM_STRING_ENTRIES':       string_entries,
                    'GROUP_ENUM_TO_BASE_ZERO_ENTRIES': '\n'.join(group_enum_to_base_zero_entries),
                    'BASE_ZERO_TO_GROUP_ENUM_ENTRIES': '\n'.join(base_zero_to_group_enum_entries),
                }

                if group_type == 'basic':
                    self.enum_to_base_zero_function_declarations.append(
                        templates.ENUM_TO_BASE_ZERO_FUNCTION_DECLARATION.format(**formatting)
                    )
                    self.base_zero_to_enum_function_declarations.append(
                        templates.BASE_ZERO_TO_ENUM_FUNCTION_DECLARATION.format(**formatting)
                    )
                    self.enum_base_zero_function_definitions.append(
                        templates.ENUM_BASE_ZERO_FUNCTION_DEFINITION.format(**formatting)
                    )
                    self.untyped_enum_string_function_declarations.append(
                        templates.UNTYPED_ENUM_STRING_FUNCTION_DECLARATION.format(**formatting)
                    )
                    self.untyped_enum_string_function_definitions.append(
                        templates.UNTYPED_ENUM_STRING_FUNCTION_DEFINITION.format(**formatting)
                    )

                self.enum_string_function_declarations.append(
                    templates.ENUM_STRING_FUNCTION_DECLARATION[group_type].format(**formatting)
                )
                self.enum_string_function_definitions.append(
                    templates.ENUM_STRING_FUNCTION_DEFINITION[group_type].format(**formatting)
                )
                self.wrapper_enum_declarations.append(
                    templates.WRAPPER_ENUM_DECLARATION.format(**formatting)
                )
                self.enum_helper_definitions.append(
                    templates.ENUM_HELPER_DEFINITION[group_type].format(**formatting)
                )

    def _generate_files(self):
        """Write output files."""
        formatters = {
            'AUTOGENERATION_WARNING':                    templates.AUTOGENERATION_WARNING,
            'COMMAND_INFO_H':                            'command_info.h',
            'MAP_MAKE_ENTRIES':                          ',\n'.join(sorted(self.map_make_entries)),
            'WRAPPER_FUNCTION_DEFINITIONS':              '\n'.join(self.wrapper_function_definitions),
            'WRAPPER_FUNCTION_DECLARATIONS':             '\n'.join(self.wrapper_function_declarations),
            'WRAPPER_ENUM_DECLARATIONS':                 util.sjoin(self.wrapper_enum_declarations),
            'ENUM_STRING_FUNCTION_DECLARATIONS':         util.sjoin(self.enum_string_function_declarations),
            'ENUM_STRING_FUNCTION_DEFINITIONS':          util.sjoin(self.enum_string_function_definitions),
            'UNTYPED_ENUM_STRING_FUNCTION_DECLARATIONS': util.sjoin(self.untyped_enum_string_function_declarations),
            'UNTYPED_ENUM_STRING_FUNCTION_DEFINITIONS':  util.sjoin(self.untyped_enum_string_function_definitions),
            'ENUM_TO_BASE_ZERO_FUNCTION_DECLARATIONS':   util.sjoin(self.enum_to_base_zero_function_declarations),
            'BASE_ZERO_TO_ENUM_FUNCTION_DECLARATIONS':   util.sjoin(self.base_zero_to_enum_function_declarations),
            'ENUM_BASE_ZERO_FUNCTION_DEFINITIONS':       util.sjoin(self.enum_base_zero_function_definitions),
            'ENUM_HELPER_DEFINITIONS':                   util.sjoin(self.enum_helper_definitions),
            'ALL_ENUM_STRING_CASES':                     util.sjoin(self.all_enum_string_cases),
            'DYNAMIC_FUNCTION_DECLARATIONS':             '\n'.join(self.dynamic_function_declarations),
            'DYNAMIC_FUNCTION_DEFINITIONS':              '\n'.join(self.dynamic_function_definitions),
            'DYNAMIC_FUNCTION_GET_STATEMENTS':           '\n    '.join(self.dynamic_function_get_statements),
            'EXTENSION_ENUM_DECLARATIONS':               self.extension_enum_declarations,
            'EXTENSION_MAP_ENTRIES':                     self.extension_map_entries,
            'EXTENSION_CASE_ENTRIES':                    self.extension_case_entries,
            'COMMAND_ENUM_DECLARATIONS':                 self.command_enum_declarations,
            'COMMAND_MAP_ENTRIES':                       self.command_map_entries,
            'COMMAND_CASE_ENTRIES':                      self.command_case_entries,
            'COMMAND_INFO_ENTRIES':                      self.command_info_entries,
        }

        content = {
            'command_info.cpp':             templates.COMMAND_INFO_CPP,
            'command_info.hpp':             templates.COMMAND_INFO_HPP,
            'dynamic_load.hpp':             templates.DYNAMIC_LOAD_HPP,
            'dynamic_load.cpp':             templates.DYNAMIC_LOAD_CPP,
            'enum_base_zero_functions.hpp': templates.ENUM_BASE_ZERO_FUNCTIONS_HPP,
            'enum_base_zero_functions.cpp': templates.ENUM_BASE_ZERO_FUNCTIONS_CPP,
            'enum_string_functions.hpp':    templates.ENUM_STRING_FUNCTIONS_HPP,
            'enum_string_functions.cpp':    templates.ENUM_STRING_FUNCTIONS_CPP,
            'wrapper_enums.hpp':            templates.WRAPPER_ENUMS_HPP,
            'wrapper_functions.cpp':        templates.WRAPPER_FUNCTIONS_CPP,
            'wrapper_functions.hpp':        templates.WRAPPER_FUNCTIONS_HPP
        }

        os.makedirs(self.outpath, exist_ok=True)
        for filename, template in content.items():
            filename = os.path.join(self.outpath, filename)
            print('GEN\t{}'.format(os.path.basename(filename)))
            try:
                with open(filename, 'w') as out_file:
                    try:
                        out_file.write(template.format(**formatters))
                    except Exception:
                        print(f'template = {template}')
                        raise
            except Exception as exception:
                print('Writing {} failed: {}'.format(filename, (exception)))
                raise

    def generate(self):
        """Pipeline parsing input XML and generating output."""
        try:
            self._parse_groups()
            self._parse_types()
            self._parse_enums()
            self._parse_features()
            self._parse_extensions()
            self._add_extra_enums()
            self._parse_and_build_commands()
            self._build_all_enums()
            self._build_enum_groups()
            self._generate_files()
        except Exception as exception:
            print('Generate failed: {}'.format(str(exception)))
            raise

def main():
    """Entry function."""
    parser = argparse.ArgumentParser(description='Generate GL wrapper from gl.xml')
    parser.add_argument('outpath', help='Output path')
    args = parser.parse_args()

    generator = GLGenerator(args.outpath)
    generator.generate()
    print('Done.')

main()
