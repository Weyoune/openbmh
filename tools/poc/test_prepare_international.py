# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic encoder/reader tests; no Original game content required."""
import unittest
from prepare_international import decode_vga, encode_vga, legacy_name, read_master, write_master, words, repeated_native_images

class ProfileDataTests(unittest.TestCase):
    def test_master_identity_and_sizes(self):
        # Deliberately unrelated tiny table counts exercise the format, not copyrighted records.
        db={'bonus':bytes(70),'pools':[b'CLUB\0',b'PLAYER\0',b'REF\0',b'GROUND\0'],
            'clubs':words([0,0]),'names':words([0,0,0]),'meta':bytes(100),'aux':bytes(8),
            'badges':words([8,9]),'strength':bytes(251),'league_aux':bytes(189),'refs':bytes(50),
            'kits':bytes([3,4]),'short':bytes(4),'squads':words([4095]*70),
            'form':words([41,42,43],4),'skills':words([0,1,2],4),'identity':words([0,1,3],4)}
        encoded=write_master(db)
        parsed=read_master(encoded,clubs=2,players=3,records=4,extra=20)
        self.assertEqual(parsed,db)
        self.assertEqual(write_master(parsed),encoded)
        with self.assertRaises(ValueError):read_master(encoded+b'\0',2,3,4,20)
        with self.assertRaises(ValueError):read_master(encoded[:-1],2,3,4,20)

    def test_vga_literal_boundaries_and_neighbours(self):
        pixels=bytearray((i*17)%256 for i in range(304*38))
        _,_,decoded=decode_vga(encode_vga(304,38,pixels))
        self.assertEqual(decoded,pixels)
        before=bytes(pixels)
        for y in range(38):pixels[y*304+76:y*304+114]=bytes([21])*38
        _,_,decoded=decode_vga(encode_vga(304,38,pixels))
        for y in range(38):
            self.assertEqual(decoded[y*304:y*304+76],before[y*304:y*304+76])
            self.assertEqual(decoded[y*304+114:(y+1)*304],before[y*304+114:(y+1)*304])
        with self.assertRaises(ValueError):encode_vga(4,4,b'123')

    def test_shared_original_placeholder(self):
        clubs=[{'crest':str(i)} for i in range(7)]
        assets={str(i):{'files':{'native':'shared' if i<6 else 'real'}} for i in range(7)}
        import hashlib
        found=repeated_native_images(clubs,assets,lambda path: {'shared':b'placeholder','real':b'individual-crest'}[path])
        self.assertEqual(found,{hashlib.sha256(b'placeholder').hexdigest()})

    def test_original_encoding(self):
        self.assertEqual(legacy_name('Müller'),b'M}LLER')
        self.assertEqual(legacy_name('Sørloth'),b'SORLOTH')
        self.assertEqual(legacy_name('Alavés'),b'ALAVES')

if __name__=='__main__':unittest.main()
