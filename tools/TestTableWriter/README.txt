TODO:
- [ ] Finish writing UiaTests.xlsx
   - Degenerate: true/false
   - Position: 1..15 (see picture)
   - Unit: Character, Word, Format*, Line, Doc
- [ ] Write script to convert .csv to tests
- [ ] Copy-paste tests into UiaTextRangeTests.cpp
- [ ] Document how this works

Notes:
- positions: origin, midTop, midBeforeDocEnd, midDocEnd, docEnd, midBeforeBufferEnd, bufferEnd
- structure: <position><P=Plus,M=Minus><#=amount><C=Character,W=Word, etc...>