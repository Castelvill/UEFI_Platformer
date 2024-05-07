#!/usr/bin/python3
import argparse

def converter(level, binary):
    with open(level, "r") as inputFile, open(binary, "wb") as outputFile:
        data = inputFile.read().splitlines()
        
        outputFile.write(len(data).to_bytes(4, "little"))
        outputFile.write(len(data[0]).to_bytes(4, "little"))
        
        tiles = 0
        
        for d in data:
            tiles += d.upper().count("G") + d.upper().count("R") + d.upper().count("M") + d.upper().count("W") + d.upper().count("S") + d.upper().count("C")

        outputFile.write(tiles.to_bytes(4, "little"))

        for d in data:
            outputFile.write(bytes(d.upper(), "ascii"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=str, help="Input: Text file with the map")
    parser.add_argument("output", type=str, help="Binary file with the map")
    args = parser.parse_args()
    converter(args.input, args.output) 
