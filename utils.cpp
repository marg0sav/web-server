#include "utils.h"
#include <cstring>

std::string getExtension(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        return path.substr(dotPos + 1); // Возвращаем расширение
    }
    return ""; // Пустая строка, если расширение не найдено
}
