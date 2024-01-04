#pragma once

#include <memory>
#include <cstdint>
#include <sstream>
#include <vector>
#include <regex>

class MezzStringBuffer
{
public:
	MezzStringBuffer(uint32_t Size = 4096);

	char* GetBuffer() const;
	uint32_t GetSize() const;

	operator char* () const;

private:
	std::unique_ptr<char[]> StringBuffer;

	char* BufferRaw;
	uint32_t BufferSize;
};

// String utils
std::vector<std::string> splitStringMulti(std::string& inputString, std::string separator = " ", bool includeSeparators = false);
std::vector<std::string> splitStringLimit(std::string& inputString, std::string separator = " ", int limit = 0);
std::string getFilename(std::string& s);
void printIniItemLine(const CInifile::Item& s);
void trim(std::string& s, const char* t = " \t\n\r\f\v");
std::string trimCopy(std::string s, const char* t = " \t\n\r\f\v");
void toLowerCase(std::string& s);
std::string toLowerCaseCopy(std::string s);
