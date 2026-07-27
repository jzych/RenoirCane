def encode(data: bytes) -> bytes:
    output = bytearray([0])
    code_index = 0
    code = 1
    for value in data:
        if value == 0:
            output[code_index] = code
            code_index = len(output)
            output.append(0)
            code = 1
        else:
            output.append(value)
            code += 1
            if code == 0xFF:
                output[code_index] = code
                code_index = len(output)
                output.append(0)
                code = 1
    output[code_index] = code
    return bytes(output)


def decode(data: bytes) -> bytes:
    output = bytearray()
    index = 0
    while index < len(data):
        code = data[index]
        index += 1
        if code == 0 or index + code - 1 > len(data):
            raise ValueError("invalid COBS frame")
        output.extend(data[index : index + code - 1])
        index += code - 1
        if code != 0xFF and index < len(data):
            output.append(0)
    return bytes(output)
