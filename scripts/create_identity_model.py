#!/usr/bin/env python3
import pathlib
import sys


def encode_varint(value):
    output = bytearray()

    while value >= 0x80:
        output.append((value & 0x7f) | 0x80)
        value >>= 7
    output.append(value)

    return bytes(output)


def field_key(field_number, wire_type):
    return encode_varint((field_number << 3) | wire_type)


def varint_field(field_number, value):
    return field_key(field_number, 0) + encode_varint(value)


def bytes_field(field_number, value):
    return field_key(field_number, 2) + encode_varint(len(value)) + value


def string_field(field_number, value):
    return bytes_field(field_number, value.encode("utf-8"))


def dimension(value):
    return varint_field(1, value)


def tensor_shape(values):
    return b"".join(bytes_field(1, dimension(value)) for value in values)


def tensor_type(values):
    tensor = varint_field(1, 1)
    tensor += bytes_field(2, tensor_shape(values))
    return bytes_field(1, tensor)


def value_info(name, values):
    message = string_field(1, name)
    message += bytes_field(2, tensor_type(values))
    return message


def node():
    message = string_field(1, "input")
    message += string_field(2, "output")
    message += string_field(3, "identity")
    message += string_field(4, "Identity")
    return message


def graph():
    message = bytes_field(1, node())
    message += string_field(2, "identity_graph")
    message += bytes_field(11, value_info("input", [1, 4]))
    message += bytes_field(12, value_info("output", [1, 4]))
    return message


def opset_import():
    return varint_field(2, 13)


def model():
    message = varint_field(1, 8)
    message += string_field(2, "ort-c-identity")
    message += bytes_field(7, graph())
    message += bytes_field(8, opset_import())
    return message


def main():
    output_path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                               else "models/identity.onnx")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(model())
    print(f"wrote {output_path} ({output_path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
