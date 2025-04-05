# This is a customized, stripped down version of https://github.com/juliettef/IconFontCppHeaders/
#  - Only does C/C++ header
#  - Does not depend on yaml or request, assumes font files are already downloaded, I do this by cmake.
#  - Hardcoded single font and output paths
#
#------------------------------------------------------------------------------


import os
import sys
import logging

if sys.version_info[0] < 3:
    raise Exception( "Python 3 or a more recent version is required." )

# Fonts

class Font:
    font_name = '[ ERROR - Missing font name ]'
    font_abbr = '[ ERROR - Mssing font abbreviation ]'
    font_minmax_abbr = ''   # optional - use if min and max defines must be differentiated. See Font Awesome Brand for example.
    font_data = '[ ERROR - Missing font data file or url ]'
    ttfs = '[ ERROR - Missing ttf ]'

    @classmethod
    def get_icons( cls, input_data ):
        # intermediate representation of the fonts data, identify the min and max
        logging.error( 'Missing implementation of class method get_icons for {!s} ]'.format( cls.font_name ))
        icons_data = {}
        icons_data.update({ 'font_min' : '[ ERROR - Missing font min ]',
                            'font_max' : '[ ERROR - Missing font max ]',
                            'icons' : '[ ERROR - Missing list of pairs [ font icon name, code ]]' })
        return icons_data

    @classmethod
    def get_intermediate_representation( cls ):
        font_ir = {}
        if os.path.isfile( cls.font_data ):
            with open( cls.font_data, 'r' ) as f:
                input_raw = f.read()
                f.close()
                logging.info( 'File read - ' + cls.font_name )
        else:
            raise Exception( 'File ' + cls.font_name + ' missing - ' + cls.font_data )
        if input_raw:
            icons_data = cls.get_icons( input_raw )
            font_ir.update( icons_data )
            font_ir.update({ 'font_data' : cls.font_data,
                             'font_name' : cls.font_name,
                             'font_abbr' : cls.font_abbr,
                             'font_minmax_abbr' : cls.font_minmax_abbr,
                             'ttfs' : cls.ttfs, })
            logging.info( 'Generated intermediate data - ' + cls.font_name )
        return font_ir

    @classmethod
    def get_icons( cls, input_data ):
        icons_data = {}
        lines = str.split( input_data, '\n' )
        if lines:
            font_min = '0x10ffff'
            font_min_int = int( font_min, 16 )
            font_max_16 = '0x0'   # 16 bit max 
            font_max_16_int = int( font_max_16, 16 )
            font_max = '0x0'
            font_max_int = int( font_max, 16 )
            icons = []
            for line in lines :
                words = str.split(line)
                if words and len( words ) >= 2:
                    word_unicode = words[ 1 ].zfill( 4 )
                    word_int = int( word_unicode, 16 )
                    if word_int < font_min_int and word_int > 0x0127 :  # exclude ASCII characters code points
                        font_min = word_unicode
                        font_min_int = word_int
                    if word_int > font_max_16_int and word_int <= 0xffff:   # exclude code points > 16 bits
                        font_max_16 = word_unicode
                        font_max_16_int = word_int
                    if word_int > font_max_int:
                        font_max = word_unicode
                        font_max_int = word_int
                    icons.append( words )
            icons_data.update({ 'font_min' : font_min,
                                'font_max_16' : font_max_16,
                                'font_max' : font_max,
                                'icons' : icons })
        return icons_data


class FontMDI( Font ):               # Pictogrammers Material Design Icons
    font_name = 'Material Design Icons'
    font_abbr = 'MDI'
    font_data_prefix = '.mdi-'
    font_data = 'res/fonts/materialdesignicons.css' # Assumed to exist - downloaded by cmake
    ttfs = [[ font_abbr, 'res/fonts/materialdesignicons-webfont.ttf' ]] # Assumed to exist - downloaded by cmake

    @classmethod
    def get_icons( cls, input_data ):
        icons_data = {}
        lines = str.split( input_data, '}\n' )
        if lines:
            font_min = '0x10ffff'
            font_min_int = int( font_min, 16 )
            font_max_16 = '0x0'   # 16 bit max 
            font_max_16_int = int( font_max_16, 16 )
            font_max = '0x0'
            font_max_int = int( font_max, 16 )
            icons = []
            for line in lines :
                if cls.font_data_prefix in line and '::before' in line:
                    font_id = line.partition( cls.font_data_prefix )[ 2 ].partition( '::before' )[ 0 ]
                    font_code = line.partition( '"\\' )[ 2 ].partition( '"' )[ 0 ].zfill( 4 )
                    font_code_int = int( font_code, 16 )
                    if font_code_int < font_min_int and font_code_int > 0x0127 :  # exclude ASCII characters code points
                        font_min = font_code
                        font_min_int = font_code_int
                    if font_code_int > font_max_16_int and font_code_int <= 0xffff:   # exclude code points > 16 bits
                        font_max_16 = font_code
                        font_max_16_int = font_code_int
                    if font_code_int > font_max_int:
                        font_max = font_code
                        font_max_int = font_code_int
                    icons.append([ font_id, font_code ])
            icons_data.update({ 'font_min' : font_min,
                                'font_max_16' : font_max_16,
                                'font_max' : font_max,
                                'icons' : icons  })
        return icons_data



# Languages


class Language:
    language_name = '[ ERROR - Missing language name ]'
    file_name = '[ ERROR - Missing file name ]'
    intermediate = {}

    def __init__( self, intermediate ):
        self.intermediate = intermediate

    @classmethod
    def prelude( cls ):
        logging.error( 'Missing implementation of class method prelude for {!s}'.format( cls.language_name ))
        return ''

    @classmethod
    def line_icon( cls, icon ):
        logging.error( 'Missing implementation of class method line_icon for {!s}'.format( cls.language_name ))
        return ''

    @classmethod
    def epilogue( cls ):
        return ''

    @classmethod
    def convert( cls ):
        result = cls.prelude()
        for icon in cls.intermediate.get( 'icons' ):
            line_icon = cls.line_icon( icon )
            result += line_icon

        abbr = cls.intermediate.get( 'font_abbr' )
        result += f'\nconst char* icon_{abbr}_names[] = {{\n'
        for icon in cls.intermediate.get( 'icons' ):
            result += cls.line_icon_name( icon )
        result += '    nullptr\n};\n\n'

        result += f'const char* icon_{abbr}_codes[] = {{\n'
        for icon in cls.intermediate.get( 'icons' ):
            result += cls.line_icon_code( icon )
        result += '    nullptr\n};\n\n'

        result += f'const unsigned int icon_{abbr}_unicodes[] = {{\n'
        for icon in cls.intermediate.get( 'icons' ):
            result += cls.line_icon_unicode( icon )
        result += '    0\n};\n\n'

        result += cls.epilogue()
        logging.info( 'Converted - {!s} for {!s}'.format( cls.intermediate.get( 'font_name' ), cls.language_name ))
        return result

    @classmethod
    def save_to_file( cls ):
        filename = "windows/IconsMaterialDesignIcons.h"
        converted = cls.convert()
        with open( filename, 'w' ) as f:
            f.write( converted )
            f.close()
        logging.info( 'Saved - {!s}'.format( filename ))


class LanguageC( Language ):
    language_name = 'C and C++'
    file_name = 'Icons{name}.h'

    @classmethod
    def prelude( cls ):
        tmpl_prelude = '// Generated by https://github.com/juliettef/IconFontCppHeaders script GenerateIconFontCppHeaders.py\n' + \
            '// for {lang}\n' + \
            '// from codepoints {font_data}\n' + \
            '// for use with font {ttf_files}\n\n' + \
            '#pragma once\n\n'
        ttf_files = []
        for ttf in cls.intermediate.get( 'ttfs' ):
            ttf_files.append( ttf[ 1 ])
        result = tmpl_prelude.format( lang = cls.language_name,
                                      font_data = cls.intermediate.get( 'font_data' ),
                                      ttf_files = ', '.join( ttf_files ))
        tmpl_prelude_define_file_name = '#define FONT_ICON_FILE_NAME_{font_abbr} "{file_name_ttf}"\n'
        for ttf in cls.intermediate.get( 'ttfs' ):
            result += tmpl_prelude_define_file_name.format( font_abbr = ttf[ 0 ], file_name_ttf = ttf[ 1 ])
        result += '\n'
        # min/max
        tmpl_line_minmax = '#define ICON_{minmax}_{abbr} 0x{val}\n'
        abbreviation = cls.intermediate.get( 'font_minmax_abbr' ) if cls.intermediate.get( 'font_minmax_abbr' ) else cls.intermediate.get('font_abbr')
        result += tmpl_line_minmax.format( minmax = 'MIN',
                                           abbr = abbreviation,
                                           val = cls.intermediate.get( 'font_min' )) + \
                  tmpl_line_minmax.format( minmax = 'MAX_16',
                                           abbr = abbreviation,
                                           val = cls.intermediate.get( 'font_max_16' )) + \
                  tmpl_line_minmax.format( minmax = 'MAX',
                                           abbr = abbreviation,
                                           val = cls.intermediate.get( 'font_max' )) + '\n'
        return result

    @classmethod
    def line_icon( cls, icon ):
        tmpl_line_icon = '#define ICON_{abbr}_{icon:<40} {code:<15} // U+{unicode:<4}\n'
        icon_name = str.upper( icon[ 0 ]).replace( '-', '_' )
        icon_code = '"' + repr( chr( int( icon[ 1 ], 16 )).encode( 'utf-8' ))[ 2:-1 ] + '"'
        result = tmpl_line_icon.format( abbr = cls.intermediate.get( 'font_abbr' ),
                                        icon = icon_name,
                                        code = icon_code,
                                        unicode =icon[ 1 ] )
        return result

    @classmethod
    def line_icon_name( cls, icon ):
        tmpl_line_icon = '    "ICON_{abbr}_{icon}",\n'
        icon_name = str.upper( icon[ 0 ]).replace( '-', '_' )
        result = tmpl_line_icon.format( abbr = cls.intermediate.get( 'font_abbr' ),
                                        icon = icon_name )
        return result

    @classmethod
    def line_icon_code( cls, icon ):
        tmpl_line_code = '    "{code}",\n'
        icon_code = repr( chr( int( icon[ 1 ], 16 )).encode( 'utf-8' ))[ 2:-1 ]
        result = tmpl_line_code.format( code = icon_code )
        return result

    @classmethod
    def line_icon_unicode( cls, icon ):
        tmpl_line_icon = '    0x{unicode},\n'
        result = tmpl_line_icon.format(unicode = icon[ 1 ])
        return result


# Main


fonts = [ FontMDI ]
languages = [ LanguageC ]
ttf2headerC = False # convert ttf files to C and C++ headers

logging.basicConfig( format='%(levelname)s : %(message)s', level = logging.WARNING )

intermediates = []
for font in fonts:
    try:
        font_intermediate = font.get_intermediate_representation()
        if font_intermediate:
            intermediates.append( font_intermediate )
    except Exception as e:
        logging.error( e )
if intermediates:
    for interm in intermediates:
        Language.intermediate = interm
        for lang in languages:
            if lang:
                lang.save_to_file()
