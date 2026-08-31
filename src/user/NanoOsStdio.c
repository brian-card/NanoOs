////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

// Standard C includes
#define FILE C_FILE
#include "stdio.h"
#undef FILE

#include "NanoOsLibC.h"
#include "../kernel/Console.h"
#include "../kernel/Hal.h"
#include "../kernel/Logger.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"

// Must come last
#include "NanoOsStdio.h"

/// @var nanoOsStdin
///
/// @brief Implementation of nanoOsStdin which is the define value for stdin.
FILE *nanoOsStdin  = (FILE*) ((intptr_t) 0x1);

/// @var nanoOsStdout
///
/// @brief Implementation of nanoOsStdout which is the define value for stdout.
FILE *nanoOsStdout = (FILE*) ((intptr_t) 0x2);

/// @var nanoOsStderr
///
/// @brief Implementation of nanoOsStderr which is the define value for stderr.
FILE *nanoOsStderr = (FILE*) ((intptr_t) 0x3);

/// @fn int printChar_(char character)
///
/// @brief Wrapper around printString for a single C character.
///
/// @param character The single char value to print.
///
/// @return Returns the number of bytes written on success, -errno on failure.
int printChar_(char character) {
  char string[2] = {character, '\0'};
  return printString(string);
}

/// @var _crlf
///
/// @brief Carriage-return/line-feed pair written in place of a bare newline.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _crlf[] KEEP_IN_FLASH = "\r\n";

/// @fn int printString_(const char *string)
///
/// @brief Wrapper around HAL->uart.write for a C string.
///
/// @return Returns the number of bytes written on success, -errno on failure.
int printString_(const char *string) {
  if (string == NULL) {
    return -EINVAL;
  }

  bool printReturnNewline = false;
  size_t stringLength = strlen(string);
  char *newlineAt = strchr(string, '\n');
  if ((newlineAt != NULL)
    && ((newlineAt == string) || (newlineAt[-1] != '\r'))
  ) {
    printReturnNewline = true;
    stringLength--;
  }

  // Find the first UART that's both online and a console.
  int32_t deviceId = 0;
  for (; ((uint32_t) deviceId) < HAL->uart.numSupported; deviceId++) {
    bool uartIsConsole = false;
    
    if ((online(HAL->uart, deviceId))
      && (HAL->uart.isConsole(deviceId, &uartIsConsole) == 0)
      && (uartIsConsole == true)
    ) {
      break;
    }
  }
  if (((uint32_t) deviceId) == HAL->uart.numSupported) {
    return -ENODEV;
  }

  ssize_t written = 0;
  int32_t rv = HAL->uart.write(deviceId, (uint8_t*) string, stringLength,
    &written);
  if (rv < 0) {
    // Bail.
    return rv;
  }
  int bytesWritten = (int) written;

  if (printReturnNewline == true) {
    rv = HAL->uart.write(deviceId, (uint8_t*) _crlf, 2, &written);
    if (rv == 0) {
      // The usual case.
      bytesWritten += (int) written;
    } else {
      bytesWritten = rv;
    }
  }

  return bytesWritten;
}

/// @fn int ullToString(unsigned long long int number, char **nextChar)
///
/// @brief Convert an unsigned long long int to its base 10 string
/// representation.
///
/// @param number The non-negative number to convert.
/// @param nextChar A double pointer to the next character in the buffer to
///   populate.
///
/// @return Returns 0 on success, -errno on failure.
int ullToString(volatile unsigned long long int number, char **nextChar) {
  if (number == 0) {
    **nextChar = '0';
    // The caller expects nextChar to be positioned one character before the
    // most-recent one written, so we need to back up one character before
    // returning.
    (*nextChar)--;
    return 0;
  }

  volatile unsigned long long int zero = 0;
  volatile unsigned long long int ten = 10;
  while (number > zero) {
    **nextChar = '0' + (number % ten);
    (*nextChar)--;
    number /= ten;
  }

  return 0;
}

/// @fn int printInt_(long long int integer)
///
/// @brief C wrapper around printString for an integer.
///
/// @param integer The integer value to print.
///
/// @return Returns the number of bytes written on success, -errno on failure.
int printInt_(long long int integer) {
  char number[20];
  number[19] = '\0';
  char *nextChar = &number[18];

  if (integer >= 0) {
    ullToString((unsigned long long int) integer, &nextChar);
    nextChar++;
  } else {
    ullToString((unsigned long long int) -integer, &nextChar);
    *nextChar = '-';
  }

  return printString(nextChar);
}

/// @var _doubleFormat
///
/// @brief sprintf() format specifier for a double value.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _doubleFormat[] KEEP_IN_FLASH = "%lf";

/// @fn int printDouble(double floatingPointValue)
///
/// @brief C wrapper around printString for a double.
///
/// @param floatingPointValue The double value to print.
///
/// @return Returns the number of bytes written on success, -errno on failure.
int printDouble(double floatingPointValue) {
  char number[20];
  sprintf(number, _doubleFormat, floatingPointValue);
  return printString(number);
}

/// @var _hexAlphabet
///
/// @brief Digits used to render a value in hexadecimal.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _hexAlphabet[] KEEP_IN_FLASH = "0123456789abcdef";

/// @fn int printHex_(unsigned long long int integer)
///
/// @brief C wrapper around printString for a hexadecimal integer.
///
/// @param integer The integer value to print in hexadecimal format.
///
/// @return Returns the number of bytes written on success, -errno on failure.
int printHex_(unsigned long long int integer) {
  char number[20];
  number[19] = '\0';
  char *nextChar = &number[18];

  if (integer > 0) {
    const char *alphabet = _hexAlphabet;
    while (integer > 0) {
      *nextChar = alphabet[integer & 0xf];
      nextChar--;
      integer >>= 4;
    }
    nextChar++;
  } else {
    *nextChar = '0';
  }

  return printString(nextChar);
}

/// @fn int printList_(const char *firstString, ...)
///
/// @brief Print a list of values.  Values are in (type, value) pairs until the
/// STOP type is reached.
///
/// @param firstString The first string value to print.
/// @param ... All following parameters are in (type, value) format.
///
/// @return Returns the number of bytes written on success, -errno on failure.
int printList_(const char *firstString, ...) {
  int returnValue = 0;
  TypeDescriptor *type = NULL;
  va_list args;

  if (firstString == NULL) {
    // Invalid.
    return -EINVAL;
  }
  printString(firstString);

  va_start(args, firstString);

  type = va_arg(args, TypeDescriptor*);
  while (type != STOP) {
    if (type == typeInt) {
      int value = va_arg(args, int);
      int rv = printInt(value);
      if (rv < 0) {
        return rv;
      }
      returnValue += rv;
    } else if (type == typeString) {
      char *value = va_arg(args, char*);
      int rv = printString(value);
      if (rv < 0) {
        return rv;
      }
      returnValue += rv;
    } else {
      logError("Invalid type %d.  Exiting parsing.\n", (int) ((intptr_t) type));
      returnValue = -EINVAL;
      break;
    }

    type = va_arg(args, TypeDescriptor*);
  }

  va_end(args);

  return returnValue;
}

/// @enum TypeModifier
///
/// @brief The type modifier parsed from a format string in a sscanf call.
typedef enum TypeModifier {
  TYPE_MODIFIER_NONE,
  TYPE_MODIFIER_HALF,
  TYPE_MODIFIER_HALF_HALF,
  TYPE_MODIFIER_INTMAX_T,
  TYPE_MODIFIER_LONG,
  TYPE_MODIFIER_LONG_LONG,
  TYPE_MODIFIER_LONG_DOUBLE,
  TYPE_MODIFIER_PTRDIFF_T,
  TYPE_MODIFIER_SIZE_T,
  NUM_TYPE_MODIFIERS
} TypeModifier;

/// @fn int scanfParseSignedInt(
///   const char **buffer, TypeModifier typeModifier, void *valuePointer)
///
/// @brief Parse a signed integer value and store it in a variable at a provided
/// pointer.
///
/// @param buffer A pointer to the character buffer that is in the process of
///   being parsed.  This value will be updated on success.
/// @param typeModifier The TypeModifier value that specifies the size of the
///   variable being stored.
/// @param valuePointer The pointer to the variable to update.
///
/// @param Returns the number of values parsed on success, -1 on failure.
int scanfParseSignedInt(
  const char **buffer, TypeModifier typeModifier, void *valuePointer
) {
  int numParsedValues = 0;
  char *nextBufferChar = NULL;

  long long value = nanoOsStrtoll(*buffer, &nextBufferChar, 0);
  if (nextBufferChar == NULL) {
    // Nothing was parsed.  Bail.
    return numParsedValues; // 0
  } else if (valuePointer == NULL) {
    numParsedValues = 1;
    *buffer = nextBufferChar;
    return numParsedValues;
  }
  

  switch (typeModifier) {
    case TYPE_MODIFIER_NONE:
      {
        int *outputPointer = (int*) valuePointer;
        *outputPointer = (int) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_HALF:
      {
        short int *outputPointer = (short int*) valuePointer;
        *outputPointer = (short int) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_HALF_HALF:
      {
        char *outputPointer = (char*) valuePointer;
        *outputPointer = (char) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_INTMAX_T:
      {
        intmax_t *outputPointer = (intmax_t*) valuePointer;
        *outputPointer = (intmax_t) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_LONG:
      {
        long *outputPointer = (long*) valuePointer;
        *outputPointer = (long) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_LONG_LONG:
      {
        long long *outputPointer = (long long*) valuePointer;
        *outputPointer = (long long) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_PTRDIFF_T:
      {
        ptrdiff_t *outputPointer = (ptrdiff_t*) valuePointer;
        *outputPointer = (ptrdiff_t) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_SIZE_T:
      {
        size_t *outputPointer = (size_t*) valuePointer;
        *outputPointer = (size_t) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    default:
      // Unrecognized TypeModifier value for parsing integer.  Error.
      numParsedValues = -1;
      break;
  }

  return numParsedValues;
}

/// @fn int scanfParseUnsignedInt(
///   const char **buffer, TypeModifier typeModifier, void *valuePointer)
///
/// @brief Parse an unsigned integer value and store it in a variable at a
/// provided pointer.
///
/// @param buffer A pointer to the character buffer that is in the process of
///   being parsed.  This value will be updated on success.
/// @param typeModifier The TypeModifier value that specifies the size of the
///   variable being stored.
/// @param valuePointer The pointer to the variable to update.
///
/// @param Returns the number of values parsed on success, -1 on failure.
int scanfParseUnsignedInt(
  const char **buffer, TypeModifier typeModifier, void *valuePointer
) {
  int numParsedValues = 0;
  char *nextBufferChar = NULL;

  unsigned long value = strtoul(*buffer, &nextBufferChar, 0);
  if (nextBufferChar == NULL) {
    // Nothing was parsed.  Bail.
    return numParsedValues; // 0
  } else if (valuePointer == NULL) {
    numParsedValues = 1;
    *buffer = nextBufferChar;
    return numParsedValues;
  }

  switch (typeModifier) {
    case TYPE_MODIFIER_NONE:
      {
        unsigned int *outputPointer = (unsigned int*) valuePointer;
        *outputPointer = (unsigned int) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_HALF:
      {
        short unsigned int *outputPointer = (short unsigned int*) valuePointer;
        *outputPointer = (short unsigned int) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_HALF_HALF:
      {
        unsigned char *outputPointer = (unsigned char*) valuePointer;
        *outputPointer = (unsigned char) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_INTMAX_T:
      {
        uintmax_t *outputPointer = (uintmax_t*) valuePointer;
        *outputPointer = (uintmax_t) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_LONG:
      {
        unsigned long *outputPointer = (unsigned long*) valuePointer;
        *outputPointer = (unsigned long) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_LONG_LONG:
      {
        unsigned long long *outputPointer = (unsigned long long*) valuePointer;
        *outputPointer = (unsigned long long) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_SIZE_T:
      {
        size_t *outputPointer = (size_t*) valuePointer;
        *outputPointer = (size_t) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    default:
      // Unrecognized TypeModifier value for parsing integer.  Error.
      numParsedValues = -1;
      break;
  }

  return numParsedValues;
}

/// @fn int scanfParseFloat(
///   const char **buffer, TypeModifier typeModifier, void *valuePointer)
///
/// @brief Parse an floating-point value and store it in a variable at a
/// provided pointer.
///
/// @param buffer A pointer to the character buffer that is in the process of
///   being parsed.  This value will be updated on success.
/// @param typeModifier The TypeModifier value that specifies the size of the
///   variable being stored.
/// @param valuePointer The pointer to the variable to update.
///
/// @param Returns the number of values parsed on success, -1 on failure.
int scanfParseFloat(
  const char **buffer, TypeModifier typeModifier, void *valuePointer
) {
  int numParsedValues = 0;
  char *nextBufferChar = NULL;

  double value = strtod(*buffer, &nextBufferChar);
  if (nextBufferChar == NULL) {
    // Nothing was parsed.  Bail.
    return numParsedValues; // 0
  } else if (valuePointer == NULL) {
    numParsedValues = 1;
    *buffer = nextBufferChar;
    return numParsedValues;
  }

  switch (typeModifier) {
    case TYPE_MODIFIER_NONE:
      {
        float *outputPointer = (float*) valuePointer;
        *outputPointer = (float) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_LONG:
      {
        double *outputPointer = (double*) valuePointer;
        *outputPointer = (double) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;

    case TYPE_MODIFIER_LONG_DOUBLE:
      {
        long double *outputPointer = (long double*) valuePointer;
        *outputPointer = (long double) value;
        numParsedValues = 1;
        *buffer = nextBufferChar;
      }
      break;


    default:
      // Unrecognized TypeModifier value for parsing integer.  Error.
      numParsedValues = -1;
      break;
  }

  return numParsedValues;
}

/// @var _whitespace
///
/// @brief Set of characters considered whitespace when scanning a string
/// field with no explicit width.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _whitespace[] KEEP_IN_FLASH = " \t\r\n";

/// @fn int scanfParseString(const char **buffer, size_t numBytes,
///   bool addNullByte, void *valuePointer)
///
/// @brief Parse a string value and store it in a variable at a provided
/// pointer.
///
/// @param buffer A pointer to the character buffer that is in the process of
///   being parsed.  This value will be updated on success.
/// @param numBytes The number of bytes to read from the buffer.
/// @param addNullByte Whether or not to add a terminating NUL byte to the end
///   of the string.
/// @param valuePointer The pointer to the variable to update.
///
/// @param Returns the number of values parsed on success, -1 on failure.
int scanfParseString(
  const char **buffer, size_t numBytes, bool addNullByte, void *valuePointer
) {
  int numParsedValues = 0;
  char *outputPointer = (char*) valuePointer;

  if (numBytes == 0) {
    // Calculate the number of bytes until the first whitespace character.
    numBytes = strcspn(*buffer, _whitespace);
  }

  if ((numBytes == 0) || (**buffer == '\0')) {
    // Nothing to parse.
    return numParsedValues; // 0
  } else if (valuePointer == NULL) {
    numParsedValues = 1;
    *buffer += numBytes;
    return numParsedValues;
  }

  memcpy(outputPointer, *buffer, numBytes);
  if (addNullByte == true) {
    outputPointer[numBytes] = '\0';
  }

  // Update the output variables.
  *buffer += numBytes;
  numParsedValues = 1;

  return numParsedValues;
}

/// @fn int vsscanf(const char *buffer, const char *format, va_list args)
///
/// @brief Read formatted input from a string into arguments provided in a
/// va_list.
///
/// @param buffer A string containing the formatted input to parse.
/// @param format The string specifying the format of the input to use.
/// @param args The va_list containing the arguments to store the parsed values
///   into.
///
/// @return Returns the number of items parsed on success, EOF on failure.
int vsscanf(const char *buffer, const char *format, va_list args) {
  const char *startOfBuffer = buffer;
  int returnValue = EOF;
  if ((buffer == NULL) || (format == NULL)) {
    return returnValue; // EOF
  }

  void *outputArg = NULL;
  while ((*buffer) && (*format)) {
    while ((*format != '%') && (*format == *buffer)) {
      format++;
      buffer++;
    }
    if ((*format != '%') && (*format != *buffer)) {
      // No more matches.  Bail.
      break;
    } else if (*format == '\0') {
      // End of match string.  Bail.
      break;
    }

    // (*format == '%')
    if (format[1] == '%') {
      if (*buffer == '%') {
        // Escaped percent matched.
        buffer++;
        format += 2;
        continue;
      } else {
        // Escaped percent *NOT* matched.
        break;
      }
    }

    // We're being asked to parse a value.  Get the pointer to store it in.
    outputArg = va_arg(args, void*);

    // Get any modifier for it.
    TypeModifier typeModifier = TYPE_MODIFIER_NONE;
    format++;
    size_t typeSize = 0;
    switch (*format) {
      case 'h':
        {
          typeModifier = TYPE_MODIFIER_HALF;
          if (format[1] == 'h') {
            typeModifier = TYPE_MODIFIER_HALF_HALF;
            format++;
          }
          format++;
        }
        break;

      case 'j':
        {
          typeModifier = TYPE_MODIFIER_INTMAX_T;
          format++;
        }
        break;

      case 'l':
        {
          typeModifier = TYPE_MODIFIER_LONG;
          if (format[1] == 'l') {
            typeModifier = TYPE_MODIFIER_LONG_LONG;
            format++;
          }
          format++;
        }
        break;

      case 'L':
      case 'q':
        {
          typeModifier = TYPE_MODIFIER_LONG_DOUBLE;
          format++;
        }
        break;

      case 't':
        {
          typeModifier = TYPE_MODIFIER_PTRDIFF_T;
          format++;
        }
        break;

      case 'z':
        {
          typeModifier = TYPE_MODIFIER_SIZE_T;
          format++;
        }
        break;

      default:
        {
          // No modifier present.  typeModifier will remain TYPE_MODIFIER_NONE.
          char formatChar = *format;
          if ((formatChar >= '0') && (formatChar <= '9')) {
            // By definition, strtoul will have to succeed, so we can just pass
            // in the pointer to the format string to have it set to the first
            // character past the size specifier.
            char *nextChar = NULL;
            typeSize = (size_t) strtoul(format, &nextChar, 10);
            if (nextChar != NULL) {
              format = (const char*) nextChar;
            }
          }
        }
        break;
    } // End of switch *format for the type modifier.

    // Now parse the value based on the conversion specifier.
    int numParsedItems = 0;
    bool addNullByte = true;
    if (*format == '*') {
      // We're being requested to parse but *NOT* store a value.  Set outputArg
      // to NULL so the parsers don't try to store the value.
    }
    switch (*format) {
      case 'd':
      case 'i':
        {
          numParsedItems
            = scanfParseSignedInt(&buffer, typeModifier, outputArg);
        }
        break;

      case 'o':
      case 'u':
      case 'x':
      case 'X':
      case 'p':
        {
          numParsedItems
            = scanfParseUnsignedInt(&buffer, typeModifier, outputArg);
        }
        break;

      case 'f':
      case 'e':
      case 'g':
      case 'E':
      case 'a':
        {
          numParsedItems
            = scanfParseFloat(&buffer, typeModifier, outputArg);
        }
        break;

      case 'c':
        {
          if (typeSize == 0) {
            // We're reading a single character.  Set typeSize to 1.
            typeSize = 1;
          }
          addNullByte = false;
        }
        // fall through
      
      case 's':
        {
          numParsedItems
            = scanfParseString(&buffer, typeSize, addNullByte, outputArg);
        }
        break;

      case 'n':
        {
          if (outputArg != NULL) {
            unsigned int bytesConsumed = (unsigned int) (
              ((uintptr_t) buffer) - ((uintptr_t) startOfBuffer));
            *((unsigned int*) outputArg) = bytesConsumed;
          }
        }
        break;

      default:
        // Unknown conversion specifier.  Do nothing.  Next pass of the while
        // loop will fail the conditional expression and we will exit parsing.
        break;
    } // End of switch *format for the conversion specifier.

    if (numParsedItems > 0) {
      if (returnValue != EOF) {
        // The usual case.
        returnValue += numParsedItems;
      } else {
        // Initialize returnValue to a valid value.
        returnValue = numParsedItems;
      }
    }

    // Increment the format to the next character to parse.
    format++;
  }

  return returnValue;
}

/// @fn int sscanf(const char *buffer, const char *format, va_list args)
///
/// @brief Read formatted input from a string into provided arguments.
///
/// @param buffer A string containing the formatted input to parse.
/// @param format The string specifying the format of the input to use.
/// @param ... The arguments to store the parsed values into.
///
/// @return Returns the number of items parsed on success, EOF on failure.
int sscanf(const char *buffer, const char *format, ...) {
  int returnValue = 0;
  va_list args;

  va_start(args, format);
  returnValue = vsscanf(buffer, format, args);
  va_end(args);

  return returnValue;
}

// Output formatting support functions (printf family).

/// @def SPRINTF_DIGITS_BUFFER_SIZE
///
/// @brief Scratch size for the digit run of the widest value rendered: a 64-bit
/// value in base 8 is 22 digits, so 24 leaves a small margin.
#define SPRINTF_DIGITS_BUFFER_SIZE 24

/// @def SPRINTF_FLOAT_MAX_PRECISION
///
/// @brief Upper bound on the number of fractional digits emitted for a floating
/// point conversion.  Keeps the 10^precision scaling factor inside a 32-bit
/// unsigned long (the eZ80 soft-float library provides unsigned-long<->double
/// conversions but not unsigned-long-long<->double).
#define SPRINTF_FLOAT_MAX_PRECISION 9

/// @def SPRINTF_UNBOUNDED
///
/// @brief Value passed as the size argument to nanoOsVsnprintf by the
/// unbounded wrappers (nanoOsVsprintf / nanoOsSprintf).
#define SPRINTF_UNBOUNDED ((size_t) -1)

/// @var _hexAlphabetUppercase
///
/// @brief Digits used to render a value in uppercase hexadecimal (%X / %p uses
/// the lowercase table above).
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _hexAlphabetUppercase[] KEEP_IN_FLASH = "0123456789ABCDEF";

/// @var _radixPrefixHexLower
///
/// @brief Alternate-form ("#") / pointer prefix for lowercase hexadecimal.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _radixPrefixHexLower[] KEEP_IN_FLASH = "0x";

/// @var _radixPrefixHexUpper
///
/// @brief Alternate-form ("#") prefix for uppercase hexadecimal.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _radixPrefixHexUpper[] KEEP_IN_FLASH = "0X";

/// @var _radixPrefixOctal
///
/// @brief Alternate-form ("#") prefix for octal.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _radixPrefixOctal[] KEEP_IN_FLASH = "0";

/// @var _sprintfNullString
///
/// @brief Rendered in place of a NULL argument to a "%s" conversion.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _sprintfNullString[] KEEP_IN_FLASH = "(null)";

/// @var _sprintfNilString
///
/// @brief Rendered in place of a NULL argument to a "%p" conversion.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _sprintfNilString[] KEEP_IN_FLASH = "(nil)";

/// @struct SprintfState
///
/// @brief Bounded output cursor threaded through the formatting helpers.
///
/// @param cursor The next position to write to.
/// @param limit One past the last writable position; the byte at limit is
///   reserved for the terminating NUL.  Data is written only while
///   cursor < limit, but length keeps counting so the return value follows
///   C99 vsnprintf semantics.
/// @param length The number of characters the complete result would contain.
typedef struct SprintfState {
  char *cursor;
  char *limit;
  size_t length;
} SprintfState;

/// @struct SprintfConversion
///
/// @brief The flags, field width, precision and length modifier parsed from a
/// single "%" conversion in the format string.
typedef struct SprintfConversion {
  bool leftJustify;    ///< "-" flag.
  bool forceSign;      ///< "+" flag.
  bool spaceSign;      ///< " " flag.
  bool alternateForm;  ///< "#" flag.
  bool zeroPad;        ///< "0" flag.
  int fieldWidth;      ///< Minimum field width, 0 if unspecified.
  int precision;       ///< Precision, -1 if unspecified.
  TypeModifier typeModifier;
} SprintfConversion;

/// @fn void sprintfPutChar(SprintfState *state, char character)
///
/// @brief Append a single character to the output, respecting the buffer limit
/// but always advancing the logical length.
///
/// @param state A pointer to the SprintfState for the call in progress.
/// @param character The character to append.
///
/// @return This function returns no value.
void sprintfPutChar(SprintfState *state, char character) {
  if (state->cursor < state->limit) {
    *(state->cursor++) = character;
  }
  state->length++;
}

/// @fn void sprintfPutFill(SprintfState *state, char character, int count)
///
/// @brief Append the same character count times.  A count <= 0 is a no-op.
///
/// @param state A pointer to the SprintfState for the call in progress.
/// @param character The character to append.
/// @param count The number of copies to append.
///
/// @return This function returns no value.
void sprintfPutFill(SprintfState *state, char character, int count) {
  for (int ii = 0; ii < count; ii++) {
    sprintfPutChar(state, character);
  }
}

/// @fn void sprintfPutRun(SprintfState *state, const char *bytes, int length)
///
/// @brief Append length bytes from a buffer.
///
/// @param state A pointer to the SprintfState for the call in progress.
/// @param bytes A pointer to the bytes to append.
/// @param length The number of bytes to append.
///
/// @return This function returns no value.
void sprintfPutRun(SprintfState *state, const char *bytes, int length) {
  for (int ii = 0; ii < length; ii++) {
    sprintfPutChar(state, bytes[ii]);
  }
}

/// @fn void sprintfRenderInteger(SprintfState *state,
///   const SprintfConversion *conversion, unsigned long long int magnitude,
///   unsigned int base, bool uppercase, char signChar, const char *radixPrefix)
///
/// @brief Render an integer magnitude with sign, optional radix prefix,
/// precision (minimum digit count) and field width.
///
/// @param state A pointer to the SprintfState for the call in progress.
/// @param conversion A pointer to the parsed SprintfConversion.
/// @param magnitude The absolute value to render.
/// @param base The numeric base (8, 10 or 16).
/// @param uppercase Whether hexadecimal digits should be uppercase.
/// @param signChar The sign character to emit ('-', '+', ' ') or '\0' for none.
/// @param radixPrefix A prefix such as "0x" to emit after the sign, or NULL.
///
/// @return This function returns no value.
void sprintfRenderInteger(
  SprintfState *state, const SprintfConversion *conversion,
  unsigned long long int magnitude, unsigned int base, bool uppercase,
  char signChar, const char *radixPrefix
) {
  char digits[SPRINTF_DIGITS_BUFFER_SIZE];
  int numDigits = 0;
  const char *alphabet = (uppercase == true) ? _hexAlphabetUppercase : _hexAlphabet;

  if (magnitude == 0) {
    digits[numDigits++] = '0';
  } else {
    while (magnitude > 0) {
      digits[numDigits++] = alphabet[(unsigned int) (magnitude % base)];
      magnitude /= base;
    }
  }

  // A precision of 0 with a value of 0 renders no digits at all.
  if ((conversion->precision == 0) && (numDigits == 1) && (digits[0] == '0')) {
    numDigits = 0;
  }

  int precisionZeros = 0;
  if (conversion->precision > numDigits) {
    precisionZeros = conversion->precision - numDigits;
  }

  int prefixLength = 0;
  if (signChar != '\0') {
    prefixLength++;
  }
  if (radixPrefix != NULL) {
    prefixLength += (int) strlen(radixPrefix);
  }

  int contentLength = prefixLength + precisionZeros + numDigits;
  int fieldPad = 0;
  if (conversion->fieldWidth > contentLength) {
    fieldPad = conversion->fieldWidth - contentLength;
  }

  // The "0" flag is ignored when left-justifying or when an explicit precision
  // was given.
  bool zeroPad = (conversion->zeroPad == true)
    && (conversion->leftJustify == false)
    && (conversion->precision < 0);

  if ((conversion->leftJustify == false) && (zeroPad == false)) {
    sprintfPutFill(state, ' ', fieldPad);
  }
  if (signChar != '\0') {
    sprintfPutChar(state, signChar);
  }
  if (radixPrefix != NULL) {
    sprintfPutRun(state, radixPrefix, (int) strlen(radixPrefix));
  }
  if (zeroPad == true) {
    sprintfPutFill(state, '0', fieldPad);
  }
  sprintfPutFill(state, '0', precisionZeros);
  while (numDigits > 0) {
    sprintfPutChar(state, digits[--numDigits]);
  }
  if (conversion->leftJustify == true) {
    sprintfPutFill(state, ' ', fieldPad);
  }
}

/// @fn void sprintfRenderString(SprintfState *state,
///   const SprintfConversion *conversion, const char *value)
///
/// @brief Render a string with optional precision (maximum length) and field
/// width.
///
/// @param state A pointer to the SprintfState for the call in progress.
/// @param conversion A pointer to the parsed SprintfConversion.
/// @param value The string to render.  NULL renders "(null)".
///
/// @return This function returns no value.
void sprintfRenderString(
  SprintfState *state, const SprintfConversion *conversion, const char *value
) {
  if (value == NULL) {
    value = _sprintfNullString;
  }

  int length = 0;
  if (conversion->precision >= 0) {
    while ((length < conversion->precision) && (value[length] != '\0')) {
      length++;
    }
  } else {
    length = (int) strlen(value);
  }

  int fieldPad = 0;
  if (conversion->fieldWidth > length) {
    fieldPad = conversion->fieldWidth - length;
  }

  if (conversion->leftJustify == false) {
    sprintfPutFill(state, ' ', fieldPad);
  }
  sprintfPutRun(state, value, length);
  if (conversion->leftJustify == true) {
    sprintfPutFill(state, ' ', fieldPad);
  }
}

/// @fn void sprintfRenderChar(SprintfState *state,
///   const SprintfConversion *conversion, char value)
///
/// @brief Render a single character with field width.
///
/// @param state A pointer to the SprintfState for the call in progress.
/// @param conversion A pointer to the parsed SprintfConversion.
/// @param value The character to render.
///
/// @return This function returns no value.
void sprintfRenderChar(
  SprintfState *state, const SprintfConversion *conversion, char value
) {
  int fieldPad = (conversion->fieldWidth > 1) ? (conversion->fieldWidth - 1) : 0;

  if (conversion->leftJustify == false) {
    sprintfPutFill(state, ' ', fieldPad);
  }
  sprintfPutChar(state, value);
  if (conversion->leftJustify == true) {
    sprintfPutFill(state, ' ', fieldPad);
  }
}

/// @fn void sprintfRenderFloat(SprintfState *state,
///   const SprintfConversion *conversion, double value)
///
/// @brief Render a non-negative-or-signed floating point value in fixed
/// notation ("%f").  Precision defaults to 6 and is clamped to
/// SPRINTF_FLOAT_MAX_PRECISION.  Exponential ("%e") and shortest ("%g") forms
/// are rendered the same as "%f" - NanoOs only ever asks for "%f" / "%lf".
///
/// The integer part is taken through a 32-bit unsigned long, so magnitudes at
/// or above 2^32 are not rendered accurately.  Every NanoOs caller formats
/// small values, so this keeps the code off the missing 64-bit soft-float
/// conversion helpers.
///
/// @param state A pointer to the SprintfState for the call in progress.
/// @param conversion A pointer to the parsed SprintfConversion.
/// @param value The value to render.
///
/// @return This function returns no value.
void sprintfRenderFloat(
  SprintfState *state, const SprintfConversion *conversion, double value
) {
  int precision = (conversion->precision < 0) ? 6 : conversion->precision;
  if (precision > SPRINTF_FLOAT_MAX_PRECISION) {
    precision = SPRINTF_FLOAT_MAX_PRECISION;
  }

  char signChar = '\0';
  if (value < 0.0) {
    signChar = '-';
    value = -value;
  } else if (conversion->forceSign == true) {
    signChar = '+';
  } else if (conversion->spaceSign == true) {
    signChar = ' ';
  }

  // Split into integer and fractional parts, scaling the fraction to the
  // requested number of digits and rounding half up.
  unsigned long int integerPart = (unsigned long int) value;
  double fraction = value - (double) integerPart;

  unsigned long int scale = 1;
  double scaleDouble = 1.0;
  for (int ii = 0; ii < precision; ii++) {
    scale *= 10;
    scaleDouble *= 10.0;
  }
  unsigned long int scaledFraction
    = (unsigned long int) ((fraction * scaleDouble) + 0.5);
  if (scaledFraction >= scale) {
    // Rounding carried into the integer part.
    scaledFraction -= scale;
    integerPart++;
  }

  // Render the integer part digits (reversed) into a scratch buffer.
  char integerDigits[SPRINTF_DIGITS_BUFFER_SIZE];
  int numIntegerDigits = 0;
  if (integerPart == 0) {
    integerDigits[numIntegerDigits++] = '0';
  } else {
    while (integerPart > 0) {
      integerDigits[numIntegerDigits++] = (char) ('0' + (int) (integerPart % 10));
      integerPart /= 10;
    }
  }

  int contentLength = numIntegerDigits;
  if (signChar != '\0') {
    contentLength++;
  }
  if (precision > 0) {
    contentLength += 1 + precision;
  }

  int fieldPad = 0;
  if (conversion->fieldWidth > contentLength) {
    fieldPad = conversion->fieldWidth - contentLength;
  }

  bool zeroPad = (conversion->zeroPad == true)
    && (conversion->leftJustify == false);

  if ((conversion->leftJustify == false) && (zeroPad == false)) {
    sprintfPutFill(state, ' ', fieldPad);
  }
  if (signChar != '\0') {
    sprintfPutChar(state, signChar);
  }
  if (zeroPad == true) {
    sprintfPutFill(state, '0', fieldPad);
  }
  while (numIntegerDigits > 0) {
    sprintfPutChar(state, integerDigits[--numIntegerDigits]);
  }
  if (precision > 0) {
    sprintfPutChar(state, '.');
    // Emit exactly "precision" fractional digits, most significant first.
    for (int ii = precision - 1; ii >= 0; ii--) {
      unsigned long int placeValue = 1;
      for (int jj = 0; jj < ii; jj++) {
        placeValue *= 10;
      }
      sprintfPutChar(state,
        (char) ('0' + (int) ((scaledFraction / placeValue) % 10)));
    }
  }
  if (conversion->leftJustify == true) {
    sprintfPutFill(state, ' ', fieldPad);
  }
}

/// @fn int nanoOsVsnprintf(char *buffer, size_t size, const char *format,
///   va_list args)
///
/// @brief Bounded, self-contained printf-family formatter.  This is the core
/// that nanoOsVsprintf / nanoOsSnprintf / nanoOsSprintf all delegate to, and it
/// is the reason NanoOs does not link libagon's nanoprintf (whose .rodata jump
/// tables are unsafe on the string-stripped firmware image).
///
/// Supported: flags "-+ #0", a decimal or "*" field width, a "." precision
/// (decimal or "*"), the length modifiers h hh l ll j z t L q, and the
/// conversions d i u o x X p c s f F e E g G and %%.
///
/// @param buffer The destination buffer.  May be NULL only when size is 0.
/// @param size The size of buffer in bytes, including the NUL.  Pass
///   SPRINTF_UNBOUNDED for the unbounded wrappers.
/// @param format The printf-style format string.
/// @param args The arguments to format.
///
/// @return Returns the number of characters that a sufficiently large buffer
/// would have received, not counting the NUL (C99 semantics), or -1 if format
/// is NULL.
int nanoOsVsnprintf(
  char *buffer, size_t size, const char *format, va_list args
) {
  if (format == NULL) {
    return -1;
  }

  SprintfState state;
  state.cursor = buffer;
  state.length = 0;
  if (size == 0) {
    state.limit = buffer;
  } else if (size == SPRINTF_UNBOUNDED) {
    state.limit = (char*) ((uintptr_t) -1);
  } else {
    state.limit = buffer + (size - 1);
  }

  for (const char *fmt = format; *fmt != '\0'; fmt++) {
    if (*fmt != '%') {
      sprintfPutChar(&state, *fmt);
      continue;
    }

    fmt++; // Consume the '%'.
    if (*fmt == '%') {
      sprintfPutChar(&state, '%');
      continue;
    }

    // Set each field explicitly rather than with an aggregate initializer: the
    // compiler would lower the initializer to a .rodata template and memcpy it
    // in, and .rodata is stripped from the shipped image.
    SprintfConversion conversion;
    conversion.leftJustify = false;
    conversion.forceSign = false;
    conversion.spaceSign = false;
    conversion.alternateForm = false;
    conversion.zeroPad = false;
    conversion.fieldWidth = 0;
    conversion.precision = -1;
    conversion.typeModifier = TYPE_MODIFIER_NONE;

    // Flags.
    bool parsingFlags = true;
    while (parsingFlags == true) {
      switch (*fmt) {
        case '-': conversion.leftJustify = true;   fmt++; break;
        case '+': conversion.forceSign = true;     fmt++; break;
        case ' ': conversion.spaceSign = true;     fmt++; break;
        case '#': conversion.alternateForm = true; fmt++; break;
        case '0': conversion.zeroPad = true;       fmt++; break;
        default:  parsingFlags = false;                   break;
      }
    }

    // Field width.
    if (*fmt == '*') {
      int width = va_arg(args, int);
      if (width < 0) {
        conversion.leftJustify = true;
        width = -width;
      }
      conversion.fieldWidth = width;
      fmt++;
    } else {
      while ((*fmt >= '0') && (*fmt <= '9')) {
        conversion.fieldWidth = (conversion.fieldWidth * 10) + (*fmt - '0');
        fmt++;
      }
    }

    // Precision.
    if (*fmt == '.') {
      fmt++;
      conversion.precision = 0;
      if (*fmt == '*') {
        int precision = va_arg(args, int);
        conversion.precision = (precision < 0) ? -1 : precision;
        fmt++;
      } else {
        while ((*fmt >= '0') && (*fmt <= '9')) {
          conversion.precision = (conversion.precision * 10) + (*fmt - '0');
          fmt++;
        }
      }
    }

    // Length modifier (same vocabulary as vsscanf above).
    switch (*fmt) {
      case 'h':
        conversion.typeModifier = TYPE_MODIFIER_HALF;
        fmt++;
        if (*fmt == 'h') {
          conversion.typeModifier = TYPE_MODIFIER_HALF_HALF;
          fmt++;
        }
        break;
      case 'l':
        conversion.typeModifier = TYPE_MODIFIER_LONG;
        fmt++;
        if (*fmt == 'l') {
          conversion.typeModifier = TYPE_MODIFIER_LONG_LONG;
          fmt++;
        }
        break;
      case 'j':
        conversion.typeModifier = TYPE_MODIFIER_INTMAX_T;
        fmt++;
        break;
      case 'z':
        conversion.typeModifier = TYPE_MODIFIER_SIZE_T;
        fmt++;
        break;
      case 't':
        conversion.typeModifier = TYPE_MODIFIER_PTRDIFF_T;
        fmt++;
        break;
      case 'L':
        conversion.typeModifier = TYPE_MODIFIER_LONG_DOUBLE;
        fmt++;
        break;
      case 'q':
        conversion.typeModifier = TYPE_MODIFIER_LONG_LONG;
        fmt++;
        break;
      default:
        break;
    }

    // Conversion specifier.
    char specifier = *fmt;

    // Pull the integer argument (if this is an integer conversion) here, in the
    // function that owns the va_list.  The width to read depends on the length
    // modifier; everything is widened to (unsigned) long long int for a single
    // rendering path.  This is done inline rather than in a helper so the
    // va_list is never passed by address (not portable - va_list is an array
    // type on some ABIs).
    long long int signedValue = 0;
    unsigned long long int unsignedValue = 0;
    bool isSignedConversion = ((specifier == 'd') || (specifier == 'i'));
    bool isUnsignedConversion = ((specifier == 'u') || (specifier == 'o')
      || (specifier == 'x') || (specifier == 'X'));

    if (isSignedConversion == true) {
      switch (conversion.typeModifier) {
        case TYPE_MODIFIER_HALF:
          signedValue = (long long int) ((short int) va_arg(args, int));
          break;
        case TYPE_MODIFIER_HALF_HALF:
          signedValue = (long long int) ((signed char) va_arg(args, int));
          break;
        case TYPE_MODIFIER_LONG:
          signedValue = (long long int) va_arg(args, long int);
          break;
        case TYPE_MODIFIER_LONG_LONG:
          signedValue = va_arg(args, long long int);
          break;
        case TYPE_MODIFIER_INTMAX_T:
          signedValue = (long long int) va_arg(args, intmax_t);
          break;
        case TYPE_MODIFIER_PTRDIFF_T:
          signedValue = (long long int) va_arg(args, ptrdiff_t);
          break;
        case TYPE_MODIFIER_SIZE_T:
          signedValue = (long long int) va_arg(args, size_t);
          break;
        default:
          signedValue = (long long int) va_arg(args, int);
          break;
      }
    } else if (isUnsignedConversion == true) {
      switch (conversion.typeModifier) {
        case TYPE_MODIFIER_HALF:
          unsignedValue = (unsigned long long int)
            ((unsigned short int) va_arg(args, unsigned int));
          break;
        case TYPE_MODIFIER_HALF_HALF:
          unsignedValue = (unsigned long long int)
            ((unsigned char) va_arg(args, unsigned int));
          break;
        case TYPE_MODIFIER_LONG:
          unsignedValue = (unsigned long long int) va_arg(args, unsigned long int);
          break;
        case TYPE_MODIFIER_LONG_LONG:
          unsignedValue = va_arg(args, unsigned long long int);
          break;
        case TYPE_MODIFIER_INTMAX_T:
          unsignedValue = (unsigned long long int) va_arg(args, uintmax_t);
          break;
        case TYPE_MODIFIER_PTRDIFF_T:
          unsignedValue = (unsigned long long int) va_arg(args, ptrdiff_t);
          break;
        case TYPE_MODIFIER_SIZE_T:
          unsignedValue = (unsigned long long int) va_arg(args, size_t);
          break;
        default:
          unsignedValue = (unsigned long long int) va_arg(args, unsigned int);
          break;
      }
    }

    switch (specifier) {
      case 'd':
      case 'i':
        {
          bool isNegative = (signedValue < 0);
          unsigned long long int magnitude = (isNegative == true)
            ? (0ULL - (unsigned long long int) signedValue)
            : (unsigned long long int) signedValue;
          char signChar = '\0';
          if (isNegative == true) {
            signChar = '-';
          } else if (conversion.forceSign == true) {
            signChar = '+';
          } else if (conversion.spaceSign == true) {
            signChar = ' ';
          }
          sprintfRenderInteger(&state, &conversion, magnitude, 10, false,
            signChar, NULL);
        }
        break;

      case 'u':
        sprintfRenderInteger(&state, &conversion, unsignedValue, 10, false,
          '\0', NULL);
        break;

      case 'o':
        {
          const char *radixPrefix
            = ((conversion.alternateForm == true) && (unsignedValue != 0))
              ? _radixPrefixOctal : NULL;
          sprintfRenderInteger(&state, &conversion, unsignedValue, 8, false,
            '\0', radixPrefix);
        }
        break;

      case 'x':
      case 'X':
        {
          bool uppercase = (specifier == 'X');
          const char *radixPrefix = NULL;
          if ((conversion.alternateForm == true) && (unsignedValue != 0)) {
            radixPrefix = (uppercase == true)
              ? _radixPrefixHexUpper : _radixPrefixHexLower;
          }
          sprintfRenderInteger(&state, &conversion, unsignedValue, 16, uppercase,
            '\0', radixPrefix);
        }
        break;

      case 'p':
        {
          uintptr_t value = (uintptr_t) va_arg(args, void*);
          if (value == 0) {
            sprintfRenderString(&state, &conversion, _sprintfNilString);
          } else {
            sprintfRenderInteger(&state, &conversion,
              (unsigned long long int) value, 16, false, '\0',
              _radixPrefixHexLower);
          }
        }
        break;

      case 'c':
        {
          char value = (char) va_arg(args, int);
          sprintfRenderChar(&state, &conversion, value);
        }
        break;

      case 's':
        {
          const char *value = va_arg(args, const char*);
          sprintfRenderString(&state, &conversion, value);
        }
        break;

      case 'f':
      case 'F':
      case 'e':
      case 'E':
      case 'g':
      case 'G':
        {
          double value = va_arg(args, double);
          sprintfRenderFloat(&state, &conversion, value);
        }
        break;

      case '\0':
        // Truncated format string.  Step back so the for-loop's ++ lands on the
        // NUL and the loop exits.
        fmt--;
        break;

      default:
        // Unknown conversion.  Emit it verbatim so the output stays debuggable.
        sprintfPutChar(&state, '%');
        sprintfPutChar(&state, specifier);
        break;
    }
  }

  if (size != 0) {
    *(state.cursor) = '\0';
  }

  return (int) state.length;
}

/// @fn int nanoOsVsprintf(char *buffer, const char *format, va_list args)
///
/// @brief Unbounded va_list printf into a caller-supplied buffer.  Redefined
/// over vsprintf by NanoOsStdio.h.
///
/// @param buffer The destination buffer.
/// @param format The printf-style format string.
/// @param args The arguments to format.
///
/// @return Returns the number of characters written, not counting the NUL.
int nanoOsVsprintf(char *buffer, const char *format, va_list args) {
  return nanoOsVsnprintf(buffer, SPRINTF_UNBOUNDED, format, args);
}

/// @fn int nanoOsSnprintf(char *buffer, size_t size, const char *format, ...)
///
/// @brief Bounded printf into a caller-supplied buffer.  Redefined over
/// snprintf by NanoOsStdio.h.
///
/// @param buffer The destination buffer.
/// @param size The size of buffer in bytes, including the NUL.
/// @param format The printf-style format string.
/// @param ... The arguments to format.
///
/// @return Returns the number of characters that would have been written, not
/// counting the NUL (C99 semantics).
int nanoOsSnprintf(char *buffer, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int returnValue = nanoOsVsnprintf(buffer, size, format, args);
  va_end(args);
  return returnValue;
}

/// @fn int nanoOsSprintf(char *buffer, const char *format, ...)
///
/// @brief Unbounded printf into a caller-supplied buffer.  Redefined over
/// sprintf by NanoOsStdio.h.
///
/// @param buffer The destination buffer.
/// @param format The printf-style format string.
/// @param ... The arguments to format.
///
/// @return Returns the number of characters written, not counting the NUL.
int nanoOsSprintf(char *buffer, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int returnValue = nanoOsVsnprintf(buffer, SPRINTF_UNBOUNDED, format, args);
  va_end(args);
  return returnValue;
}

// Input support functions.

/// @fn ConsoleBuffer* nanoOsWaitForInput(void)
///
/// @brief Wait for input from the nanoOs port owned by the current process.
///
/// @return Returns a pointer to the input retrieved on success, NULL on
/// failure.
ConsoleBuffer* nanoOsWaitForInput(void) {
  ConsoleBuffer *consoleBuffer = NULL;
  FileDescriptor *inputFd = schedulerGetFileDescriptor(stdin);
  if (inputFd == NULL) {
    logError("Could not get input file descriptor for process %d"
      " and stream %d.\n", getRunningPid(), (int) (intptr_t) stdin);

    // We can't proceed, so bail.
    return consoleBuffer; // NULL
  }
  IoChannel *inputChannel = &inputFd->inputChannel;

  if (inputChannel->pid == SCHEDULER_STATE->consolePid) {
    // Tell the console that we're waiting for input.  Fire and forget.
    (void) initSendProcessMessageToPid(
      inputChannel->pid, inputChannel->messageType,
      /* data= */ 0, /* size= */ 0, false);
  }

  if (inputChannel->pid != PROCESS_ID_NOT_SET) {
    ProcessMessage *response
      = processMessageQueueWaitForType(
      CONSOLE_COMMAND_SIGNATURE | CONSOLE_RETURNING_INPUT, NULL);
    consoleBuffer = (ConsoleBuffer*) processMessageData(response);

    if (processMessageWaiting(response) == false) {
      // The usual case.
      processMessageRelease(response);
    } else {
      // Just tell the sender that we're done.
      processMessageSetDone(response);
    }
  }

  return consoleBuffer;
}

/// @fn char *nanoOsFgets(char *buffer, int size, FILE *stream)
///
/// @brief Custom implementation of fgets for this library.
///
/// @param buffer The character buffer to write the captured input into.
/// @param size The maximum number of bytes to write into the buffer.
/// @param stream A pointer to the FILE stream to read from.  Currently, only
///   stdin is supported.
///
/// @return Returns the buffer pointer provided on success, NULL on failure.
char *nanoOsFgets(char *buffer, int size, FILE *stream) {
  char *returnValue = NULL;

  if (size > 0) {
    size_t bytesRead = nanoOsFread(buffer, 1, size - 1, stream);
    if (bytesRead > 0) {
      buffer[bytesRead] = '\0';
      returnValue = buffer;
    }
  }

  return returnValue;
}

/// @fn int nanoOsVfscanf(FILE *stream, const char *format, va_list args)
///
/// @brief Read formatted input from a file stream into arguments provided in
/// a va_list.
///
/// @param stream A pointer to the FILE stream to read from.  Currently, only
///   stdin is supported.
/// @param format The string specifying the expected format of the input data.
/// @param args The va_list containing the arguments to store the parsed values
///   into.
///
/// @return Returns the number of items parsed on success, EOF on failure.
int nanoOsVfscanf(FILE *stream, const char *format, va_list args) {
  int returnValue = EOF;

  if (stream == stdin) {
    ConsoleBuffer *consoleBuffer = nanoOsWaitForInput();
    if (consoleBuffer == NULL) {
      return returnValue; // EOF
    }

    returnValue = vsscanf(consoleBuffer->buffer, format, args);
    // Release the buffer.  Fire and forget.
    (void) initSendProcessMessageToPid(
      SCHEDULER_STATE->consolePid,
      CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_BUFFER,
      /* data= */ consoleBuffer, /* size= */ sizeof(*consoleBuffer), false);
  }

  return returnValue;
}

/// @fn int nanoOsFscanf(FILE *stream, const char *format, ...)
///
/// @brief Read formatted input from a file stream into provided arguments.
///
/// @param stream A pointer to the FILE stream to read from.  Currently, only
///   stdin is supported.
/// @param format The string specifying the expected format of the input data.
/// @param ... The arguments to store the parsed values into.
///
/// @return Returns the number of items parsed on success, EOF on failure.
int nanoOsFscanf(FILE *stream, const char *format, ...) {
  int returnValue = EOF;
  va_list args;

  va_start(args, format);
  returnValue = nanoOsVfscanf(stream, format, args);
  va_end(args);

  return returnValue;
}

/// @fn int nanoOsScanf(const char *format, ...)
///
/// @brief Read formatted input from the nanoOs into provided arguments.
///
/// @param format The string specifying the expected format of the input data.
/// @param ... The arguments to store the parsed values into.
///
/// @return Returns the number of items parsed on success, EOF on failure.
int nanoOsScanf(const char *format, ...) {
  int returnValue = EOF;
  va_list args;

  va_start(args, format);
  returnValue = nanoOsVfscanf(stdin, format, args);
  va_end(args);

  return returnValue;
}

// Output support functions.

/// @fn ConsoleBuffer* nanoOsGetBuffer(void)
///
/// @brief Get a buffer from the runConsole process by sending it a command
/// message and getting its response.
///
/// @return Returns a pointer to a ConsoleBuffer from the runConsole process on
/// success, NULL on failure.
ConsoleBuffer* nanoOsGetBuffer(void) {
  ConsoleGetBufferArgs consoleGetBufferArgs = {
    .consoleBuffer = NULL,
  };

  // It's possible that all the nanoOs buffers are in use at the time this call
  // is made, so we may have to try multiple times.  Do a while loop until we
  // get a buffer back or until an error occurs.
  while (consoleGetBufferArgs.consoleBuffer == NULL) {
    ProcessMessage *processMessage = initSendProcessMessageToPid(
      SCHEDULER_STATE->consolePid,
      CONSOLE_COMMAND_SIGNATURE | CONSOLE_GET_BUFFER,
      &consoleGetBufferArgs, sizeof(consoleGetBufferArgs), true);
    if (processMessage == NULL) {
      break; // will return returnValue, which is NULL
    }

    // We want to make sure the handler is done processing the message before
    // we wait for a reply.  Do a blocking wait.
    if (processMessageWaitForDone(processMessage, NULL) != processSuccess) {
      // Something is wrong.  Bail.
      processMessageRelease(processMessage);
      break; // will return returnValue, which is NULL
    }

    processMessageRelease(processMessage);
    if (consoleGetBufferArgs.consoleBuffer == NULL) {
      // Yield control to give the OS a chance to get done processing the
      // buffers that are in use.
      processYield();
    }
  }

  return consoleGetBufferArgs.consoleBuffer;
}

/// @fn int nanoOsWriteBuffer(FILE *stream, ConsoleBuffer *consoleBuffer)
///
/// @brief Send a CONSOLE_WRITE_BUFFER command to the nanoOs process.
///
/// @param stream A pointer to a FILE object designating which file to output
///   to (stdout or stderr).
/// @param consoleBuffer A pointer to a ConsoleBuffer previously returned from
///   a call to nanoOsGetBuffer.
///
/// @return Returns 0 on success, EOF on failure.
int nanoOsWriteBuffer(FILE *stream, ConsoleBuffer *consoleBuffer) {
  int returnValue = 0;
  if ((stream == stdout) || (stream == stderr)) {
    FileDescriptor *outputFd = schedulerGetFileDescriptor(stream);
    if (outputFd == NULL) {
      logError("Could not get output file descriptor for process %d"
        " and stream %d\n", getRunningPid(), (int) (intptr_t) stream);

      // Release the buffer to avoid creating a leak.  Fire and forget.
      (void) initSendProcessMessageToPid(
        SCHEDULER_STATE->consolePid,
        CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_BUFFER,
        /* data= */ consoleBuffer, /* size= */ sizeof(*consoleBuffer), false);

      // We can't proceed, so bail.
      returnValue = EOF;
      return returnValue;
    }
    IoChannel *outputChannel = &outputFd->outputChannel;

    if ((outputChannel != NULL) && (outputChannel->pid != PROCESS_ID_NOT_SET)) {
      if ((stream == stdout) || (stream == stderr)) {
        ProcessMessage *processMessage = initSendProcessMessageToPid(
          outputChannel->pid, outputChannel->messageType,
          /* data= */ consoleBuffer, /* size= */ sizeof(*consoleBuffer), true);
        if (processMessage != NULL) {
          processMessageWaitForDone(processMessage, NULL);
          processMessageRelease(processMessage);
        } else {
          returnValue = EOF;
        }
      } else {
        logError("Request to write to invalid stream %d from process %d.\n",
          (int) (intptr_t) stream, getRunningPid());

        // Release the buffer to avoid creating a leak.  Fire and forget.
        (void) initSendProcessMessageToPid(
          SCHEDULER_STATE->consolePid,
          CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_BUFFER,
          /* data= */ consoleBuffer, /* size= */ sizeof(*consoleBuffer), false);

        returnValue = EOF;
      }
    } else {
      logError("Request to write with no output pipe set from process %d.\n",
        getRunningPid());

      // Release the buffer to avoid creating a leak.  Fire and forget.
      (void) initSendProcessMessageToPid(
        SCHEDULER_STATE->consolePid,
        CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_BUFFER,
        /* data= */ consoleBuffer, /* size= */ sizeof(*consoleBuffer), false);

      returnValue = EOF;
    }
  } else {
    // stream is a regular FILE.
    FilesystemIoCommandArgs filesystemIoCommandArgs = {
      .file = stream,
      .buffer = consoleBuffer->buffer,
      .length = (uint32_t) strlen(consoleBuffer->buffer)
    };
    ProcessMessage *processMessage = initSendProcessMessageToPid(
      SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_WRITE_FILE,
      /* data= */ &filesystemIoCommandArgs,
      /* size= */ sizeof(filesystemIoCommandArgs),
      true);
    processMessageWaitForDone(processMessage, NULL);
    if (filesystemIoCommandArgs.length == 0) {
      returnValue = EOF;
    }
    processMessageRelease(processMessage);

    // Release the buffer to avoid creating a leak.  Fire and forget.
    (void) initSendProcessMessageToPid(
      SCHEDULER_STATE->consolePid,
      CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_BUFFER,
      /* data= */ consoleBuffer, /* size= */ sizeof(*consoleBuffer), false);
  }

  return returnValue;
}

/// @fn int nanoOsFputs(const char *s, FILE *stream)
///
/// @brief Print a raw string to a file stream.
///
/// @param s A pointer to the string to print.
/// @param stream The file stream to print to.  Ignored by this function.
///
/// @return This function always returns 0.
int nanoOsFputs(const char *s, FILE *stream) {
  // This implementation is almost identical to nanoOsFwrite.  Really, we should
  // just call that.  But, we won't do that because we're concerned about stack
  // space.
  int returnValue = 0;
  size_t len = strlen(s);
  if (nanoOsFwrite(s, 1, len, stream) != len) {
    returnValue = EOF;
  }

  return returnValue;
}

/// @fn int nanoOsVfprintf(FILE *stream, const char *format, va_list args)
///
/// @brief Print a formatted string to the nanoOs.  Gets a string buffer from
/// the nanoOs, writes the formatted string to that buffer, then sends a
/// command to the nanoOs to print the buffer.  If the stream being printed to
/// is stderr, blocks until the buffer is printed to the nanoOs.
///
/// @param stream A pointer to the FILE stream to print to (stdout or stderr).
/// @param format The format string for the printf message.
/// @param args The va_list of arguments that were passed into one of the
///   higher-level printf functions.
///
/// @return Returns the number of bytes printed on success, -1 on error.
int nanoOsVfprintf(FILE *stream, const char *format, va_list args) {
  int returnValue = -1;
  ConsoleBuffer *consoleBuffer = nanoOsGetBuffer();
  if (consoleBuffer == NULL) {
    // Nothing we can do.
    return returnValue;
  }

  returnValue
    = vsnprintf(consoleBuffer->buffer, CONSOLE_BUFFER_SIZE, format, args);
  if (nanoOsWriteBuffer(stream, consoleBuffer) == EOF) {
    returnValue = -1;
  }

  return returnValue;
}

/// @fn int nanoOsFprintf(FILE *stream, const char *format, ...)
///
/// @brief Print a formatted string to the nanoOs.  Constructs a va_list from
/// the arguments provided and then calls nanoOsVfprintf.
///
/// @param stream A pointer to the FILE stream to print to (stdout or stderr).
/// @param format The format string for the printf message.
/// @param ... Any additional arguments needed by the format string.
///
/// @return Returns the number of bytes printed on success, -1 on error.
int nanoOsFprintf(FILE *stream, const char *format, ...) {
  int returnValue = 0;
  va_list args;

  va_start(args, format);
  returnValue = nanoOsVfprintf(stream, format, args);
  va_end(args);

  return returnValue;
}

/// @fn int nanoOsFprintf(FILE *stream, const char *format, ...)
///
/// @brief Print a formatted string to stdout.  Constructs a va_list from the
/// arguments provided and then calls nanoOsVfprintf with stdout as the first
/// parameter.
///
/// @param format The format string for the printf message.
/// @param ... Any additional arguments needed by the format string.
///
/// @return Returns the number of bytes printed on success, -1 on error.
int nanoOsPrintf(const char *format, ...) {
  int returnValue = 0;
  va_list args;

  va_start(args, format);
  returnValue = nanoOsVfprintf(stdout, format, args);
  va_end(args);

  return returnValue;
}

/// @fn int nanoOsFileno(FILE *stream)
///
/// @brief Get the numeric ID of the underlying file descriptor of a FILE
/// stream.
///
/// @param stream A pointer to the FILE stream to examine.
///
/// @return Returns the file descriptor value of the underlying file on success,
/// -1 on failure.  On failure, the value of errno is also set to indicate the
/// reason for the failure.
int nanoOsFileno(FILE *stream) {
  if (stream == NULL) {
    errno = EBADF;
    return -1;
  } else if (stream == nanoOsStdin) {
    return 0;
  } else if (stream == nanoOsStdout) {
    return 1;
  } else if (stream == nanoOsStderr) {
    return 2;
  }
  
  return stream->fd;
}

/// @fn size_t nanoOsFread(void *ptr, size_t size, size_t nmemb, FILE *stream)
///
/// @brief Custom implementation of fread for this library.
///
/// @param ptr A pointer to the memory to read data into.
/// @param size The size, in bytes, of each element that is to be read from the
///   file.
/// @param nmemb The number of elements that are to be read from the file.
/// @param stream A pointer to the previously-opened file.
///
/// @return Returns the total number of objects successfully read from the
/// file.
size_t nanoOsFread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t returnValue = 0;
  char *charBuffer = (char*) ptr;
  ConsoleBuffer *consoleBuffer
    = (ConsoleBuffer*) getProcessStorage(FGETS_CONSOLE_BUFFER_KEY);
  size_t numBytesReceived = 0;
  char *newlineAt = NULL;
  int numBytesToCopy = 0;
  size_t nanoOsInputLength = 0;
  int bufferIndex = 0;
  size_t bufferSize = size * nmemb;

  if (stream == stdin) {
    // There are four stop conditions:
    // 1. nanoOsWaitForInput returns NULL, signalling the end of the input
    //    from the stream.
    // 2. We read a newline.
    // 3. We read an escape sequence.
    // 4. We reach bufferSize - 1 bytes received from the stream.
    if (consoleBuffer == NULL) {
      consoleBuffer = nanoOsWaitForInput();
      setProcessStorage(FGETS_CONSOLE_BUFFER_KEY, consoleBuffer);
    } else {
      // We're continuing to read from a buffer that contained a newline plus
      // something else after it.
      newlineAt = strchr(consoleBuffer->buffer, ASCII_NEWLINE);
      if (newlineAt == NULL) {
        newlineAt = strchr(consoleBuffer->buffer, ASCII_RETURN);
      }
      if (newlineAt != NULL) {
        bufferIndex = (((uintptr_t) newlineAt)
          - ((uintptr_t) consoleBuffer->buffer)) + 1;
      } else {
        // This should be impossible given the algorithm below, but assume
        // nothing.
      }
    }

    while (
      (consoleBuffer != NULL)
      && (newlineAt == NULL)
      && (numBytesReceived < bufferSize)
    ) {
      newlineAt = strchr(&consoleBuffer->buffer[bufferIndex], '\n');
      if (newlineAt == NULL) {
        newlineAt = strchr(consoleBuffer->buffer, '\r');
      }

      if ((newlineAt == NULL) || (newlineAt[1] == '\0')) {
        // The usual case.
        nanoOsInputLength = (int) strlen(&consoleBuffer->buffer[bufferIndex]);
      } else {
        // We've received a buffer that contains a newline plus something after
        // it.  Copy everything up to and including the newline.  Return what
        // we copy and leave the pointer alone so that it's picked up on the
        // next call.
        nanoOsInputLength = (int) (((uintptr_t) newlineAt)
          - ((uintptr_t) &consoleBuffer->buffer[bufferIndex]));
      }

      numBytesToCopy
        = MIN((bufferSize - numBytesReceived), nanoOsInputLength);
      memcpy(&charBuffer[numBytesReceived], &consoleBuffer->buffer[bufferIndex],
        numBytesToCopy);
      numBytesReceived += numBytesToCopy;
      charBuffer[numBytesReceived] = '\0';
      // Release the buffer.  Fire and forget.
      (void) initSendProcessMessageToPid(
        SCHEDULER_STATE->consolePid,
        CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_BUFFER,
        /* data= */ consoleBuffer, /* size= */ sizeof(*consoleBuffer), false);

      if ((newlineAt != NULL)
        || (strchr(consoleBuffer->buffer, ASCII_ESCAPE))
      ) {
        // We've reached one of the stop cases, so we're not going to attempt
        // to receive any more data from the file descriptor.
        consoleBuffer = NULL;
      } else {
        // There was no newline in this message.  We need to get another one.
        consoleBuffer = nanoOsWaitForInput();
        bufferIndex = 0;
      }

      setProcessStorage(FGETS_CONSOLE_BUFFER_KEY, consoleBuffer);
    }
    returnValue = numBytesReceived;
  } else {
    // stream is a regular FILE.
    returnValue = filesystemFRead(ptr, size, nmemb, stream);
  }

  return returnValue;
}

/// @fn size_t nanoOsFwrite(const void *ptr, size_t size, size_t nmemb,
///   FILE *stream)
///
/// @brief Write data to a previously-opened file or output stream.
///
/// @param ptr A pointer to the memory to write data from.
/// @param size The size, in bytes, of each element that is to be written to
///   the file.
/// @param nmemb The number of elements that are to be written to the file.
/// @param stream A pointer to the previously-opened file.
///
/// @return Returns the total number of objects successfully written to the
/// file.
size_t nanoOsFwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t bytesRemaining = size * nmemb;
  size_t bytesWritten = 0;
  const char *charPointer = (const char*) ptr;

  while (bytesRemaining > 0) {
    ConsoleBuffer *consoleBuffer = nanoOsGetBuffer();
    if (consoleBuffer == NULL) {
      // Nothing we can do.  Return what we've written so far.
      return bytesWritten / size;
    }

    size_t bytesToCopy = MIN(bytesRemaining, CONSOLE_BUFFER_SIZE - 1);
    memcpy(consoleBuffer->buffer, charPointer, bytesToCopy);
    consoleBuffer->buffer[bytesToCopy] = '\0';
    if (nanoOsWriteBuffer(stream, consoleBuffer) == 0) {
      bytesWritten += bytesToCopy;
      charPointer += bytesToCopy;
      bytesRemaining -= bytesToCopy;
    } else {
      break;
    }
  }

  return bytesWritten / size;
}

