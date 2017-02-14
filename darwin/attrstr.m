// 12 february 2017
#import "uipriv_darwin.h"

// this is what AppKit does internally
// WebKit does this too; see https://github.com/adobe/webkit/blob/master/Source/WebCore/platform/graphics/mac/GraphicsContextMac.mm
static NSColor *spellingColor = nil;
static NSColor *grammarColor = nil;
static NSColor *auxiliaryColor = nil;

static NSColor *tryColorNamed(NSString *name)
{
	NSImage *img;

	img = [NSImage imageNamed:name];
	if (img == nil)
		return nil;
	return [NSColor colorWithPatternImage:img];
}

void initUnderlineColors(void)
{
	spellingColor = tryColorNamed(@"NSSpellingDot");
	if (spellingColor == nil) {
		// WebKit says this is needed for "older systems"; not sure how old, but 10.11 AppKit doesn't look for this
		spellingColor = tryColorNamed(@"SpellingDot");
		if (spellingColor == nil)
			spellingColor = [NSColor redColor];
	}
	[spellingColor retain];		// override autoreleasing

	grammarColor = tryColorNamed(@"NSGrammarDot");
	if (grammarColor == nil) {
		// WebKit says this is needed for "older systems"; not sure how old, but 10.11 AppKit doesn't look for this
		grammarColor = tryColorNamed(@"GrammarDot");
		if (grammarColor == nil)
			grammarColor = [NSColor greenColor];
	}
	[grammarColor retain];		// override autoreleasing

	auxiliaryColor = tryColorNamed(@"NSCorrectionDot");
	if (auxiliaryColor == nil) {
		// WebKit says this is needed for "older systems"; not sure how old, but 10.11 AppKit doesn't look for this
		auxiliaryColor = tryColorNamed(@"CorrectionDot");
		if (auxiliaryColor == nil)
			auxiliaryColor = [NSColor blueColor];
	}
	[auxiliaryColor retain];		// override autoreleasing
}

void uninitUnderlineColors(void)
{
	[auxiliaryColor release];
	[grammarColor release];
	[spellingColor release];
}

// unlike the other systems, Core Text rolls family, size, weight, italic, width, AND opentype features into the "font" attribute
// TODO opentype features and AAT fvar table info is lost, so a handful of fonts in the font panel ("Titling" variants of some fonts and Skia and possibly others but those are the examples I know about) cannot be represented by uiDrawFontDescriptor; what *can* we do about this since this info is NOT part of the font on other platforms?
// TODO see if we could use NSAttributedString?
// TODO consider renaming this struct and the fep variable(s)
struct foreachParams {
	CFMutableAttributedStringRef mas;
	NSMutableDictionary *converted;
	uiDrawFontDescriptor *defaultFont;
	NSMutableArray *backgroundBlocks;
};

#define maxFeatures 32

struct fontParams {
	uiDrawFontDescriptor desc;
	uint16_t featureTypes[maxFeatures];
	uint16_t featureSpecifiers[maxFeatures];
	size_t nFeatures;
};

static void ensureFontInRange(struct foreachParams *p, size_t start, size_t end)
{
	size_t i;
	NSNumber *n;
	struct fontParams *new;

	for (i = start; i < end; i++) {
		n = [NSNumber numberWithInteger:i];
		if ([p->converted objectForKey:n] != nil)
			continue;
		new = uiNew(struct fontParams);
		new->desc = *(p->defaultFont);
		[p->converted setObject:[NSValue valueWithPointer:new] forKey:n];
	}
}

static void adjustFontInRange(struct foreachParams *p, size_t start, size_t end, void (^adj)(struct fontParams *fp))
{
	size_t i;
	NSNumber *n;
	NSValue *v;

	for (i = start; i < end; i++) {
		n = [NSNumber numberWithInteger:i];
		v = (NSValue *) [p->converted objectForKey:n];
		adj((struct fontParams *) [v pointerValue]);
	}
}

static backgroundBlock mkBackgroundBlock(size_t start, size_t end, double r, double g, double b, double a)
{
	return Block_copy(^(uiDrawContext *c, uiDrawTextLayout *layout, double x, double y) {
		uiDrawBrush brush;

		brush.Type = uiDrawBrushTypeSolid;
		brush.R = r;
		brush.G = g;
		brush.B = b;
		brush.A = a;
		drawTextBackground(c, x, y, layout, start, end, &brush, 0);
	});
}

static CGColorRef mkcolor(uiAttributeSpec *spec)
{
	CGColorSpaceRef colorspace;
	CGColorRef color;
	CGFloat components[4];

	colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	if (colorspace == NULL) {
		// TODO
	}
	components[0] = spec->R;
	components[1] = spec->G;
	components[2] = spec->B;
	components[3] = spec->A;
	color = CGColorCreate(colorspace, components);
	CFRelease(colorspace);
	return color;
}

static int processAttribute(uiAttributedString *s, uiAttributeSpec *spec, size_t start, size_t end, void *data)
{
	struct foreachParams *p = (struct foreachParams *) data;
	CFRange range;
	CGColorRef color;
	size_t ostart, oend;
	backgroundBlock block;
	int32_t us;
	CFNumberRef num;

	ostart = start;
	oend = end;
	start = attrstrUTF8ToUTF16(s, start);
	end = attrstrUTF8ToUTF16(s, end);
	range.location = start;
	range.length = end - start;
	switch (spec->Type) {
	case uiAttributeFamily:
		ensureFontInRange(p, start, end);
		adjustFontInRange(p, start, end, ^(struct fontParams *fp) {
			fp->desc.Family = (char *) (spec->Value);
		});
		break;
	case uiAttributeSize:
		ensureFontInRange(p, start, end);
		adjustFontInRange(p, start, end, ^(struct fontParams *fp) {
			fp->desc.Size = spec->Double;
		});
		break;
	case uiAttributeWeight:
		ensureFontInRange(p, start, end);
		adjustFontInRange(p, start, end, ^(struct fontParams *fp) {
			fp->desc.Weight = (uiDrawTextWeight) (spec->Value);
		});
		break;
	case uiAttributeItalic:
		ensureFontInRange(p, start, end);
		adjustFontInRange(p, start, end, ^(struct fontParams *fp) {
			fp->desc.Italic = (uiDrawTextItalic) (spec->Value);
		});
		break;
	case uiAttributeStretch:
		ensureFontInRange(p, start, end);
		adjustFontInRange(p, start, end, ^(struct fontParams *fp) {
			fp->desc.Stretch = (uiDrawTextStretch) (spec->Value);
		});
		break;
	case uiAttributeColor:
		color = mkcolor(spec);
		CFAttributedStringSetAttribute(p->mas, range, kCTForegroundColorAttributeName, color);
		CFRelease(color);
		break;
	case uiAttributeBackground:
		block = mkBackgroundBlock(ostart, oend,
			spec->R, spec->G, spec->B, spec->A);
		[p->backgroundBlocks addObject:block];
		Block_release(block);
		break;
	case uiAttributeVerticalForms:
		if (spec->Value != 0)
			CFAttributedStringSetAttribute(p->mas, range, kCTVerticalFormsAttributeName, kCFBooleanTrue);
		else
			CFAttributedStringSetAttribute(p->mas, range, kCTVerticalFormsAttributeName, kCFBooleanFalse);
		break;
	case uiAttributeUnderline:
		switch (spec->Value) {
		case uiDrawUnderlineStyleNone:
			us = kCTUnderlineStyleNone;
			break;
		case uiDrawUnderlineStyleSingle:
			us = kCTUnderlineStyleSingle;
			break;
		case uiDrawUnderlineStyleDouble:
			us = kCTUnderlineStyleDouble;
			break;
		case uiDrawUnderlineStyleSuggestion:
			// TODO incorrect if a solid color
			us = kCTUnderlineStyleThick;
			break;
		}
		num = CFNumberCreate(NULL, kCFNumberSInt32Type, &us);
		CFAttributedStringSetAttribute(p->mas, range, kCTUnderlineStyleAttributeName, num);
		CFRelease(num);
		break;
	case uiAttributeUnderlineColor:
		switch (spec->Value) {
		case uiDrawUnderlineColorCustom:
			color = mkcolor(spec);
			break;
		case uiDrawUnderlineColorSpelling:
			color = [spellingColor CGColor];
			break;
		case uiDrawUnderlineColorGrammar:
			color = [grammarColor CGColor];
			break;
		case uiDrawUnderlineColorAuxiliary:
			color = [auxiliaryColor CGColor];
			break;
		}
		CFAttributedStringSetAttribute(p->mas, range, kCTUnderlineColorAttributeName, color);
		if (spec->Value == uiDrawUnderlineColorCustom)
			CFRelease(color);
		break;
	// TODO
	}
	return 0;
}

static CTFontRef fontdescToCTFont(uiDrawFontDescriptor *fd)
{
	CTFontDescriptorRef desc;
	CTFontRef font;

	desc = fontdescToCTFontDescriptor(fd);
	font = CTFontCreateWithFontDescriptor(desc, fd->Size, NULL);
	CFRelease(desc);			// TODO correct?
	return font;
}

static void applyAndFreeFontAttributes(struct foreachParams *p)
{
	[p->converted enumerateKeysAndObjectsUsingBlock:^(NSNumber *key, NSValue *val, BOOL *stop) {
		struct fontParams *fp;
		CTFontRef font;
		CFRange range;

		fp = (struct fontParams *) [val pointerValue];
		font = fontdescToCTFont(&(fp->desc));
		range.location = [key integerValue];
		range.length = 1;
		CFAttributedStringSetAttribute(p->mas, range, kCTFontAttributeName, font);
		CFRelease(font);
		uiFree(fp);
	}];
}

static const CTTextAlignment ctaligns[] = {
	[uiDrawTextLayoutAlignLeft] = kCTTextAlignmentLeft,
	[uiDrawTextLayoutAlignCenter] = kCTTextAlignmentCenter,
	[uiDrawTextLayoutAlignRight] = kCTTextAlignmentRight,
};

static CTParagraphStyleRef mkParagraphStyle(uiDrawTextLayoutParams *p)
{
	CTParagraphStyleRef ps;
	CTParagraphStyleSetting settings[16];
	size_t nSettings = 0;

	settings[nSettings].spec = kCTParagraphStyleSpecifierAlignment;
	settings[nSettings].valueSize = sizeof (CTTextAlignment);
	settings[nSettings].value = ctaligns + p->Align;
	nSettings++;

	ps = CTParagraphStyleCreate(settings, nSettings);
	if (ps == NULL) {
		// TODO
	}
	return ps;
}

CFAttributedStringRef attrstrToCoreFoundation(uiDrawTextLayoutParams *p, NSArray **backgroundBlocks)
{
	CFStringRef cfstr;
	CFMutableDictionaryRef defaultAttrs;
	CTFontRef defaultCTFont;
	CTParagraphStyleRef ps;
	CFAttributedStringRef base;
	CFMutableAttributedStringRef mas;
	struct foreachParams fep;

	cfstr = CFStringCreateWithCharacters(NULL, attrstrUTF16(p->String), attrstrUTF16Len(p->String));
	if (cfstr == NULL) {
		// TODO
	}
	defaultAttrs = CFDictionaryCreateMutable(NULL, 0,
		&kCFCopyStringDictionaryKeyCallBacks,
		&kCFTypeDictionaryValueCallBacks);
	if (defaultAttrs == NULL) {
		// TODO
	}
	defaultCTFont = fontdescToCTFont(p->DefaultFont);
	CFDictionaryAddValue(defaultAttrs, kCTFontAttributeName, defaultCTFont);
	CFRelease(defaultCTFont);
	ps = mkParagraphStyle(p);
	CFDictionaryAddValue(defaultAttrs, kCTParagraphStyleAttributeName, ps);
	CFRelease(ps);

	base = CFAttributedStringCreate(NULL, cfstr, defaultAttrs);
	if (base == NULL) {
		// TODO
	}
	CFRelease(cfstr);
	CFRelease(defaultAttrs);
	mas = CFAttributedStringCreateMutableCopy(NULL, 0, base);
	CFRelease(base);

	CFAttributedStringBeginEditing(mas);
	fep.mas = mas;
	fep.converted = [NSMutableDictionary new];
	fep.defaultFont = p->DefaultFont;
	fep.backgroundBlocks = [NSMutableArray new];
	uiAttributedStringForEachAttribute(p->String, processAttribute, &fep);
	applyAndFreeFontAttributes(&fep);
	[fep.converted release];
	CFAttributedStringEndEditing(mas);

	*backgroundBlocks = fep.backgroundBlocks;
	return mas;
}