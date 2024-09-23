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
std::vector<std::string> splitStringMulti(const std::string& inputString, std::string separator = " ", bool includeSeparators = false, bool trimStrings = false);
std::vector<std::string> splitStringLimit(const std::string& inputString, std::string separator = " ", int limit = 0, bool trimStrings = false);
std::string getFilename(std::string& s);
void printIniItemLine(const CInifile::Item& s);
void trim(std::string& s, const char* t = " \t\n\r\f\v");
std::string trimCopy(std::string s, const char* t = " \t\n\r\f\v");
void toLowerCase(std::string& s);
std::string toLowerCaseCopy(std::string s);
void replaceAll(std::string& str, const std::string& from, const std::string& to);
std::string replaceAllCopy(std::string str, const std::string& from, const std::string& to);
