// vi:noai:sw=4

#include "terminol/common/support.hxx"

void test(uint8_t num, const char ascii[2]) {
    char    tmpAscii[2];
    byteToHex(num, tmpAscii[0], tmpAscii[1]);
    ENFORCE(tmpAscii[0] == ascii[0] && tmpAscii[1] == ascii[1],
           "Mismatch byte --> hex: " <<
           tmpAscii[0] << tmpAscii[1] << " == " << ascii[0] << ascii[1]);

    uint8_t tmpNum = hexToByte(ascii[0], ascii[1]);
    ENFORCE(tmpNum == num, "Mismatch hex --> byte: " << tmpNum << " == " << num);
}

int main() {
    test(0, "00");
    test(255, "FF");
    test(127, "7F");

    return 0;
}
